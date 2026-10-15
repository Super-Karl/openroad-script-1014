// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "delgpu/DelGpu.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "GraphGraphics.h"
#include "delgpu/Gpu.h"
#include "graph/BuildGraph.h"
#include "odb/db.h"
#include "optimize/RunOptimization.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"
#include "web/core.h"

namespace delgpu {

DelGpu::DelGpu(utl::Logger* logger) : logger_(logger)
{
}

void DelGpu::run(bool gui,
                 bool verify,
                 bool gpu,
                 int iterations,
                 double max_step,
                 double jump_offset_nm,
                 int frame_ms,
                 const char* report,
                 int max_backtracks,
                 double learning_rate,
                 double learning_rate_end,
                 const char* learning_rate_mode)
{
  if (running_) {
    throw std::runtime_error("DELGPU is already running");
  }
  const std::string mode = gpu ? (learning_rate_mode ? learning_rate_mode
                                                     : kDefaultLearningRateMode)
                               : "linear";
  if (!gpu) {
    learning_rate = learning_rate_end = 1.0;
  }
  if (mode != "linear" && mode != "adaptive") {
    throw std::invalid_argument("Unknown DELGPU learning-rate mode");
  }
  if (learning_rate_end == 0) {
    learning_rate_end
        = mode == "adaptive" ? std::min(learning_rate, 0.5) : learning_rate;
  }
  if (mode == "adaptive" && (!gpu || learning_rate_end > learning_rate)) {
    throw std::invalid_argument(
        "Adaptive mode requires GPU and end <= initial rate");
  }
  if (iterations < 0 || frame_ms < 0 || !std::isfinite(max_step)
      || max_step <= 0 || !std::isfinite(jump_offset_nm) || jump_offset_nm <= 0
      || max_backtracks < 0 || max_backtracks > 19
      || !std::isfinite(learning_rate) || learning_rate <= 0
      || !std::isfinite(max_step * learning_rate)
      || max_step * learning_rate <= 0 || !std::isfinite(learning_rate_end)
      || learning_rate_end <= 0 || !std::isfinite(max_step * learning_rate_end)
      || max_step * learning_rate_end <= 0) {
    throw std::invalid_argument("Invalid DELGPU iteration parameters");
  }
  struct RunningGuard
  {
    bool& flag;
    explicit RunningGuard(bool& f) : flag(f) { flag = true; }
    ~RunningGuard() { flag = false; }
  } running(running_);
  if (gui && !web::Gui::enabled()) {
    throw std::runtime_error(
        "DELGPU: -gui 1 requires an active GUI; launch OpenROAD with -gui");
  }
  if (verify && !gpu) {
    throw std::runtime_error("DELGPU: verification requires the GPU backend");
  }
  logger_->report(
      "DELGPU parameters: backend={} gui={} max_iterations={} max_step={}W "
      "jump_offset_nm={} max_backtracks={} learning_rate={} "
      "effective_max_step={}W learning_rate_end={} learning_rate_mode={}",
      gpu ? "gpu" : "cpu",
      gui,
      iterations,
      max_step,
      jump_offset_nm,
      gpu ? max_backtracks : 19,
      gpu ? learning_rate : 1.0,
      gpu ? max_step * learning_rate : max_step,
      gpu ? learning_rate_end : 1.0,
      mode);
  clearGraphGraphics();
  auto* block = init();
  if (block == nullptr) {
    return;
  }
  if (gpu) {
    const auto info = initializeGpu();
    logger_->report("GPU {}: {}", info.device, info.name);
    logger_->report(
        "GPU memory: {:.2f} GiB",
        static_cast<double>(info.total_memory) / (1024 * 1024 * 1024));
    logger_->report("Compute capability: {}.{}", info.major, info.minor);
    logger_->report("Streaming multiprocessors: {}", info.multiprocessors);
    logger_->report("CUDA driver/runtime: {}.{}/{}.{}",
                    info.driver_version / 1000,
                    (info.driver_version % 1000) / 10,
                    info.runtime_version / 1000,
                    (info.runtime_version % 1000) / 10);
    logger_->report("gpu ready");
  }
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  std::vector<LayerNodes> graphs;
  size_t total_nodes = 0, total_arcs = 0;
  int processed = 0;
  for (const char* name : {"M2", "M3", "M4", "M5", "M6", "M7"}) {
    auto* layer = block->getTech()->findLayer(name);
    if (layer == nullptr || layer->getRoutingLevel() == 0
        || layer->getWidth() <= 0
        || (layer->getDirection() != odb::dbTechLayerDir::HORIZONTAL
            && layer->getDirection() != odb::dbTechLayerDir::VERTICAL)) {
      logger_->report("DELGPU: skipping layer {} (missing or invalid)", name);
      continue;
    }
    auto graph
        = buildLayerGraph(block, layer, total_nodes, gpu, verify, logger_);
    total_nodes += graph.nodes.size();
    total_arcs += graph.arcs.size();
    ++processed;
    // Graph-only headless runs release each layer immediately. Iteration
    // retains the host graph; display copies are created only with GUI enabled.
    if (gui || iterations > 0) {
      graphs.push_back(std::move(graph));
    }
  }
  const auto built = Clock::now();
  if (gui) {
    if (iterations > 0) {
      showGraphGraphics(block, std::vector<LayerNodes>(graphs));
    } else {
      showGraphGraphics(block, std::move(graphs));
    }
  }
  const auto shown = Clock::now();
  logger_->report(
      "DELGPU complete: backend={} gui={} verify={} layers={} nodes={} arcs={} "
      "build_ms={:.3f} display_ms={:.3f}",
      gpu ? "gpu" : "cpu",
      gui,
      verify,
      processed,
      total_nodes,
      total_arcs,
      std::chrono::duration<double, std::milli>(built - start).count(),
      std::chrono::duration<double, std::milli>(shown - built).count());
  if (iterations > 0) {
    runOptimization(block,
                    graphs,
                    gpu,
                    gui,
                    iterations,
                    max_step,
                    jump_offset_nm,
                    frame_ms,
                    report == nullptr ? "" : report,
                    max_backtracks,
                    learning_rate,
                    learning_rate_end,
                    mode,
                    logger_);
  }
}

odb::dbBlock* DelGpu::init()
{
  // Resolve the current block on every run so read_db cannot leave stale state.
  auto* openroad = ord::OpenRoad::openRoad();
  if (openroad == nullptr || openroad->getDb() == nullptr
      || openroad->getDb()->getChip() == nullptr
      || openroad->getDb()->getChip()->getBlock() == nullptr) {
    logger_->report("DELGPU init: no design database is loaded");
    return nullptr;
  }

  auto* block = openroad->getDb()->getChip()->getBlock();
  auto* tech = block->getTech();
  if (tech == nullptr) {
    logger_->report("DELGPU init: no technology is loaded");
    return nullptr;
  }

  // Use DEL's counts of routed nets and special-wire objects/boxes.
  std::size_t routed_nets = 0;
  std::size_t special_wire_objects = 0;
  std::size_t special_wire_boxes = 0;
  for (auto* net : block->getNets()) {
    if (net->getWire() != nullptr) {
      ++routed_nets;
    }
    for (auto* swire : net->getSWires()) {
      ++special_wire_objects;
      special_wire_boxes += swire->getWires().size();
    }
  }

  const auto iterm_count = block->getITerms().size();
  const auto bterm_count = block->getBTerms().size();
  logger_->report(
      "DELGPU init: design={} insts={} nets={} pins={} (iterms={}, bterms={}) "
      "routed_nets={} special_wire_objects={} special_wire_boxes={} "
      "routing_layers={} route_layer_range=[{},{}] dbu_per_micron={}",
      block->getName(),
      block->getInsts().size(),
      block->getNets().size(),
      iterm_count + bterm_count,
      iterm_count,
      bterm_count,
      routed_nets,
      special_wire_objects,
      special_wire_boxes,
      tech->getRoutingLayerCount(),
      block->getMinRoutingLayer(),
      block->getMaxRoutingLayer(),
      block->getDbUnitsPerMicron());

  const auto die = block->getDieArea();
  const double dbu = block->getDbUnitsPerMicron();
  if (dbu > 0) {
    logger_->report(
        "DELGPU die: bounds=({:.3f},{:.3f})-({:.3f},{:.3f}) um "
        "size={:.3f}x{:.3f} um",
        die.xMin() / dbu,
        die.yMin() / dbu,
        die.xMax() / dbu,
        die.yMax() / dbu,
        die.dx() / dbu,
        die.dy() / dbu);
  }
  return block;
}

}  // namespace delgpu
