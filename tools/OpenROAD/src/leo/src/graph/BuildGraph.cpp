// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <array>
#include <vector>

#include "leo/Leo.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "ord/OpenRoad.hh"
#include "utl/Logger.h"

namespace leo {

odb::dbBlock* Leo::init()
{
  auto* openroad = ord::OpenRoad::openRoad();
  if (openroad == nullptr || openroad->getDb() == nullptr
      || openroad->getDb()->getChip() == nullptr
      || openroad->getDb()->getChip()->getBlock() == nullptr) {
    logger_->report("LEO init: no design database is loaded");
    return nullptr;
  }

  odb::dbBlock* block = openroad->getDb()->getChip()->getBlock();
  odb::dbTech* tech = block->getTech();

  if (tech == nullptr) {
    logger_->report("LEO init: no technology is loaded");
    return nullptr;
  }

  size_t routed_nets = 0;
  size_t special_wire_objects = 0;
  size_t special_wire_boxes = 0;
  for (odb::dbNet* net : block->getNets()) {
    if (net->getWire() != nullptr) {
      ++routed_nets;
    }
    for (odb::dbSWire* swire : net->getSWires()) {
      ++special_wire_objects;
      special_wire_boxes += swire->getWires().size();
    }
  }

  const size_t iterm_count = block->getITerms().size();
  const size_t bterm_count = block->getBTerms().size();
  const int routing_layer_count
      = tech == nullptr ? 0 : tech->getRoutingLayerCount();
  const int min_routing_layer = block->getMinRoutingLayer();
  const int max_routing_layer = block->getMaxRoutingLayer();

  logger_->report(
      "LEO init: design={} insts={} nets={} pins={} (iterms={}, bterms={}) "
      "routed_nets={} special_wire_objects={} special_wire_boxes={} "
      "routing_layers={} "
      "route_layer_range=[{},{}] dbu_per_micron={}",
      block->getName(),
      block->getInsts().size(),
      block->getNets().size(),
      iterm_count + bterm_count,
      iterm_count,
      bterm_count,
      routed_nets,
      special_wire_objects,
      special_wire_boxes,
      routing_layer_count,
      min_routing_layer,
      max_routing_layer,
      block->getDbUnitsPerMicron());
  return block;
}

void Leo::buildGraph(odb::dbBlock* block,
                     odb::dbTechLayer* layer,
                     int prune_factor)
{
  const int width = layer->getWidth();
  const bool horizontal
      = layer->getDirection() == odb::dbTechLayerDir::HORIZONTAL;
  const size_t first_trim = trims_.size();
  // The trims of supply nets are not generated, but their wire metal still
  // blocks the movement of a trim.
  std::vector<odb::Rect> obstacles;
  for (auto* net : block->getNets()) {
    if (net->getWire() == nullptr) {
      continue;
    }
    const bool supply = net->getSigType().isSupply();
    std::vector<odb::Rect> metal;
    auto add_shape = [&](const odb::dbShape& shape) {
      if (shape.getTechLayer() != layer) {
        return;
      }
      obstacles.push_back(shape.getBox());
      if (!supply) {
        metal.push_back(shape.getBox());
      }
    };
    odb::dbWireShapeItr it;
    odb::dbShape shape;
    for (it.begin(net->getWire()); it.next(shape);) {
      if (shape.isVia()) {
        std::vector<odb::dbShape> boxes;
        odb::dbShape::getViaBoxes(shape, boxes);
        for (const auto& box : boxes) {
          add_shape(box);
        }
      } else {
        add_shape(shape);
      }
    }
    if (metal.empty()) {
      continue;
    }
    for (auto& trim : makeTrimBoxes(
             metal, width, horizontal, layer->getName(), net->getId())) {
      trims_.push_back(trim);
    }
  }

  // Each trim is one Vnode; the layer rules scale its basic width into D/Dt/Du.
  const LayerRules rules = makeLayerRules(width, prune_factor);
  LayerNodes layer_nodes;
  layer_nodes.layer = layer->getName();
  layer_nodes.horizontal = horizontal;
  layer_nodes.pitch = layer->getPitch();
  layer_nodes.dbu = block->getDbUnitsPerMicron();
  layer_nodes.rules = rules;
  layer_nodes.nodes = makeVnodes(trims_, first_trim, trims_.size(), horizontal);
  computeDomains(layer_nodes, obstacles, true);
  const size_t clipped = pruneDomains(layer_nodes, prune_factor);
  buildArcs(layer_nodes);
  collectComponents(layer_nodes);
  layer_nodes_.push_back(std::move(layer_nodes));

  const LayerNodes& built = layer_nodes_.back();
  const size_t trim_count = trims_.size() - first_trim;
  const size_t node_count = built.nodes.size();
  std::array<size_t, 3> arc_counts{};
  for (const Arc& arc : built.arcs) {
    ++arc_counts[static_cast<size_t>(arc.type)];
  }
  const int64_t cap2 = 2 * rules.move_limit;
  size_t capped = 0;
  size_t blocked = 0;
  int64_t max_move2 = 0;
  for (const Vnode& node : built.nodes) {
    capped += (node.l_move2 == cap2 || node.u_move2 == cap2) ? 1 : 0;
    blocked += node.blocked ? 1 : 0;
    max_move2 = std::max(max_move2, std::max(node.l_move2, node.u_move2));
  }

  const double dbu = block->getDbUnitsPerMicron();
  logger_->report(
      "LEO pruning: layer={} k={} P={:.6f}um clipped={} "
      "solver=fresh_full_scans ema=first_improvement",
      layer->getName(),
      prune_factor,
      rules.move_limit / dbu,
      clipped);
  logger_->report("LEO trims: layer={} width={:.6f}um height={:.6f}um count={}",
                  layer->getName(),
                  width / dbu,
                  2.0 * width / dbu,
                  trim_count);
  logger_->report(
      "LEO vnodes: layer={} count={} D={:.6f}um Dt={:.6f}um Du={:.6f}um "
      "({}x/{}x/{}x width)",
      layer->getName(),
      node_count,
      rules.d / dbu,
      rules.dt / dbu,
      rules.du / dbu,
      kDWidthMultiplier,
      kDtWidthMultiplier,
      kDuWidthMultiplier);
  logger_->report(
      "LEO domains: layer={} limit={:.6f}um capped={} blocked={} "
      "max_move={:.6f}um",
      layer->getName(),
      rules.move_limit / dbu,
      capped,
      blocked,
      max_move2 / 2.0 / dbu);
  logger_->report("LEO subgraphs: layer={} total={} with_arcs={}",
                  layer->getName(),
                  built.components.size(),
                  static_cast<size_t>(std::count_if(
                      built.components.begin(),
                      built.components.end(),
                      [](const Component& c) { return !c.arcs.empty(); })));
  logger_->report("LEO arcs: layer={} A={} B={} C={} total={}",
                  layer->getName(),
                  arc_counts[static_cast<size_t>(ArcType::kA)],
                  arc_counts[static_cast<size_t>(ArcType::kB)],
                  arc_counts[static_cast<size_t>(ArcType::kC)],
                  built.arcs.size());
  logger_->report("LEO step 1/3: layer={} {} trims -> {} vnodes -> {} arcs",
                  layer->getName(),
                  trim_count,
                  node_count,
                  built.arcs.size());
}

}  // namespace leo
