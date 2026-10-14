// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "delgpu/DelGpu.h"

#include <cstddef>

#include "delgpu/Gpu.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

namespace delgpu {

DelGpu::DelGpu(utl::Logger* logger) : logger_(logger)
{
}

void DelGpu::run()
{
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
  init();
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
