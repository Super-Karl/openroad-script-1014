// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%{
#include "del/Del.h"
#include "GraphGraphics.h"
#include "TrimGraphics.h"
#include "OptimizationGraphics.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
%}

%include "../../Exception.i"

%inline %{

namespace del {
int configured_iterations_cmd() { return global_parameters.max_iterations; }
double configured_max_step_cmd() { return global_parameters.max_step_w; }
double configured_jump_offset_cmd() { return global_parameters.jump_offset_nm; }
void configure_cmd(int iterations, double max_step, double jump_offset)
{
  ord::OpenRoad::openRoad()->getDel()->configure(iterations, max_step, jump_offset);
}


// Return a Tcl wide integer: OpenDB's uint64_t return type currently lacks
// a Tcl typemap. Used to measure regular routed wires before/after DEL.
unsigned long long regular_wire_length_dbu_cmd()
{
  auto* chip = ord::OpenRoad::openRoad()->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  unsigned long long total = 0;
  if (block != nullptr) {
    for (auto* net : block->getNets()) {
      if (auto* wire = net->getWire()) {
        total += wire->getLength();
      }
    }
  }
  return total;
}

void run_cmd(int max_iterations, int frame_ms, const char* report, bool step_mode)
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* tool = openroad->getDel();
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  tool->run(max_iterations, report, [&](int iteration, const auto& positions) {
    return showOptimizationFrame(block, tool->getLayerNodes(), positions,
                                 iteration, step_mode ? 0 : frame_ms, tool->isPostprocessed());
  }, step_mode);
}

void next_cmd()
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* tool = openroad->getDel();
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  tool->next([&](int iteration, const auto& positions) {
    return showOptimizationFrame(block, tool->getLayerNodes(), positions,
                                 iteration, 0, tool->isPostprocessed());
  });
}

void continue_cmd()
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* tool = openroad->getDel();
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  tool->continueRun([&](int iteration, const auto& positions) {
    return showOptimizationFrame(block, tool->getLayerNodes(), positions,
                                 iteration, 50, tool->isPostprocessed());
  });
}

}  // namespace del

%}
