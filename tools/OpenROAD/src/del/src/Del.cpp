// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "del/Del.h"

#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include "odb/db.h"
#include "utl/Logger.h"

namespace del {
DelParameters global_parameters;
namespace {
void validateParameters(const DelParameters& p)
{
  if (p.max_iterations < 1 || !std::isfinite(p.max_step_w) || p.max_step_w <= 0
      || !std::isfinite(p.jump_offset_nm) || p.jump_offset_nm <= 0) {
    throw std::invalid_argument(
        "DEL requires positive max_iterations and finite positive max_step (W) "
        "/ jump_offset_nm");
  }
}
}  // namespace

void Del::configure(int max_iterations,
                    double max_step_w,
                    double jump_offset_nm)
{
  if (running_) {
    throw std::runtime_error("Cannot configure DEL during an active iteration");
  }
  const DelParameters candidate{max_iterations, max_step_w, jump_offset_nm};
  validateParameters(candidate);
  global_parameters = candidate;
}

Del::Del(utl::Logger* logger) : logger_(logger)
{
}

void Del::run(int max_iterations,
              const std::string& report,
              const FrameCallback& frame,
              bool step_mode)
{
  if (running_) {
    throw std::runtime_error("DEL is already running");
  }
  const DelParameters parameters = global_parameters;
  validateParameters(parameters);
  if (max_iterations == -1) {
    max_iterations = parameters.max_iterations;
  }
  if (max_iterations < 0) {
    throw std::invalid_argument("DEL iteration count must be nonnegative");
  }
  logger_->report(
      "DEL run parameters: max_iterations={} max_step={}W jump_offset_nm={} "
      "(configured max_iterations={})",
      max_iterations,
      parameters.max_step_w,
      parameters.jump_offset_nm,
      parameters.max_iterations);
  running_ = true;
  struct RunningGuard
  {
    bool& running;
    ~RunningGuard() { running = false; }
  } guard{running_};
  using Clock = std::chrono::steady_clock;
  const auto run_start = Clock::now();

  const auto milliseconds = [](Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
  };

  // Clear once per invocation; keep completed layers for the combined display.
  session_.reset();
  displacements_.clear();
  postprocessed_ = false;
  trims_.clear();
  layer_nodes_.clear();
  auto* block = init();
  double layers_total_ms = 0.0;
  int processed_layers = 0;
  if (block != nullptr) {
    constexpr std::array<const char*, 6> layers{
        "M2", "M3", "M4", "M5", "M6", "M7"};
    for (const char* name : layers) {
      auto* layer = block->getTech()->findLayer(name);
      if (layer == nullptr || layer->getRoutingLevel() == 0
          || layer->getWidth() == 0
          || (layer->getDirection() != odb::dbTechLayerDir::HORIZONTAL
              && layer->getDirection() != odb::dbTechLayerDir::VERTICAL)) {
        logger_->report(
            "DEL: skipping layer {} (missing or invalid routing layer)", name);
        continue;
      }

      logger_->report("DEL layer {}: begin", name);
      const auto layer_start = Clock::now();
      buildGraph(block, layer);
      const auto layer_end = Clock::now();

      const double layer_ms = milliseconds(layer_start, layer_end);
      layers_total_ms += layer_ms;
      ++processed_layers;
      logger_->report(
          "DEL timing (ms): layer={} buildGraph={:.3f} total={:.3f}",
          name,
          layer_ms,
          layer_ms);
    }
  }
  size_t node_total = 0;
  size_t blocked_total = 0;
  std::array<size_t, 3> arc_totals{};
  for (const auto& layer_nodes : layer_nodes_) {
    node_total += layer_nodes.nodes.size();
    for (const Arc& arc : layer_nodes.arcs) {
      ++arc_totals[static_cast<size_t>(arc.type)];
    }
    for (const Vnode& node : layer_nodes.nodes) {
      blocked_total += node.blocked ? 1 : 0;
    }
  }
  logger_->report("DEL vnodes: layers={} total={} trims_total={}",
                  layer_nodes_.size(),
                  node_total,
                  trims_.size());
  logger_->report(
      "DEL domains: layers={} blocked={}", layer_nodes_.size(), blocked_total);
  logger_->report("DEL arcs: layers={} A={} B={} C={} total={}",
                  layer_nodes_.size(),
                  arc_totals[static_cast<size_t>(ArcType::kA)],
                  arc_totals[static_cast<size_t>(ArcType::kB)],
                  arc_totals[static_cast<size_t>(ArcType::kC)],
                  arc_totals[0] + arc_totals[1] + arc_totals[2]);
  const double dbu = block == nullptr ? 1 : block->getDbUnitsPerMicron();
  try {
    optimize(max_iterations, dbu, report, frame, step_mode, block, parameters);
  } catch (...) {
    session_.reset();
    throw;
  }
  logger_->report("DEL total timing (ms): layers={} total={:.3f} run={:.3f}",
                  processed_layers,
                  layers_total_ms,
                  milliseconds(run_start, Clock::now()));
}

}  // namespace del
