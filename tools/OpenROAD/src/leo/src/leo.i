// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

%{
#include "GraphGraphics.h"
#include "TrimGraphics.h"
#include "leo/Leo.h"
#include "leo/Measurement.h"
#include "odb/db.h"
#include "ord/OpenRoad.hh"
%}

%include "../../Exception.i"

%inline %{

namespace leo {

void report_quality_cmd(const char* report)
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  reportQuality(*openroad->getLeo(), block,
                report == nullptr ? "" : report, openroad->getLogger());
}

void audit_stages_cmd(const char* report)
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  auditStages(*openroad->getLeo(), block,
              report == nullptr ? "" : report, openroad->getLogger());
}

void run_cmd(const char* report, int prune_factor)
{
  auto* openroad = ord::OpenRoad::openRoad();
  auto* tool = openroad->getLeo();
  tool->run(prune_factor);
  auto* chip = openroad->getDb()->getChip();
  auto* block = chip == nullptr ? nullptr : chip->getBlock();
  showTrims(block, tool->getTrims());
  showVnodes(block, tool->getLayerNodes());
  showArcs(block, tool->getLayerNodes());
  showMoves(block, tool->getLayerNodes());
  if (block != nullptr) {
    reportQuality(*tool, block, report == nullptr ? "" : report,
                  openroad->getLogger());
  }
}

}  // namespace leo

%}
