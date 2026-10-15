// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "leo/Ema.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../ComponentModel.h"
#include "TightTree.h"
#include "leo/Graph.h"
#include "leo/Leo.h"
#include "utl/Logger.h"

namespace leo {
namespace {

// Weight of a node in the objective: a trim at the high end of the metal adds
// metal when it moves up, one at the low end when it moves down.
int64_t weightOf(const Vnode& node)
{
  return node.high_end ? 1 : -1;
}

// Constraints of the ranges LPA accepted; the arcs without a range are left out
// so an unsatisfied rule can still be reported later.
void acceptedConstraints(const LayerNodes& layer,
                         const Component& component,
                         std::vector<Constraint>& out)
{
  out.clear();
  for (int arc_id : component.arcs) {
    const Arc& arc = layer.arcs[arc_id];
    if (arc.selected_range < 0) {
      continue;
    }
    const ArcRange& range = arc.ranges[arc.selected_range];
    if (range.has_low) {
      out.push_back({arc.src, arc.dst, range.low2});
    }
    if (range.has_high) {
      out.push_back({arc.dst, arc.src, -range.high2});
    }
  }
}

// Metal added by the moves of the subgraph, doubled.
int64_t extentLength2(const LayerNodes& layer,
                      const Component& component,
                      const std::vector<int64_t>& value)
{
  int64_t length = 0;
  for (int id : component.nodes) {
    const Vnode& node = layer.nodes[id];
    length += weightOf(node) * (value[id] - along2(node, layer.horizontal));
  }
  return length;
}

const char* statusName(EmaResult::Status status)
{
  switch (status) {
    case EmaResult::Status::kUnchanged:
      return "unchanged";
    case EmaResult::Status::kImproved:
      return "improved";
    case EmaResult::Status::kIterationLimit:
      return "iteration_limit";
  }
  return "?";
}

}  // namespace

std::vector<EmaResult> solveEma(LayerNodes& layer)
{
  std::vector<EmaResult> results;
  layer.ema = EmaStats{};
  const size_t node_count = layer.nodes.size();
  if (node_count == 0) {
    return results;
  }

  layer.ema.subgraphs = layer.components.size();
  results.resize(layer.components.size());
  for (size_t index = 0; index < layer.components.size(); ++index) {
    const Component& component = layer.components[index];
    EmaResult& result = results[index];
    result.nodes = component.nodes.size();
    result.arcs = component.arcs.size();
    if (component.arcs.empty()) {
      continue;
    }

    LayerNodes model = componentModel(layer, component);
    std::vector<int64_t> value;
    Component local_component;
    for (const Vnode& node : model.nodes) {
      value.push_back(posAlong2(node, model.horizontal));
      local_component.nodes.push_back(node.id);
    }
    for (const Arc& arc : model.arcs) {
      local_component.arcs.push_back(arc.id);
    }
    std::vector<Constraint> constraints;
    acceptedConstraints(model, local_component, constraints);
    result.length_before2 = extentLength2(model, local_component, value);
    size_t moves = 0;
    // The paper does not prescribe a pivot order. Use ascending node IDs,
    // one direction towards the pivot's ideal, and commit the first improving
    // tree immediately. Restart after each move; never rank candidate gains.
    while (true) {
      bool improved = false;
      for (const Vnode& pivot : model.nodes) {
        const int direction = pivot.high_end ? -1 : 1;
        const MoveCandidate move
            = evaluateMove(model, constraints, value, pivot.id, direction);
        if (move.cost2 >= 0) {
          continue;
        }
        for (int id : move.nodes) {
          value[id] += direction * move.step2;
        }
        ++moves;
        improved = true;
        break;
      }
      if (!improved) {
        break;
      }
    }

    for (size_t id = 0; id < model.nodes.size(); ++id) {
      Vnode& node = layer.nodes[component.nodes[id]];
      if (layer.horizontal) {
        node.pos_x2 = value[id];
      } else {
        node.pos_y2 = value[id];
      }
    }

    result.length_after2 = extentLength2(model, local_component, value);
    result.iterations = moves;
    result.moves = moves;
    if (moves > 0) {
      result.status = EmaResult::Status::kImproved;
      ++layer.ema.improved;
    } else {
      result.status = EmaResult::Status::kUnchanged;
      ++layer.ema.unchanged;
    }
    layer.ema.moves += moves;
    layer.ema.length_before2 += result.length_before2;
    layer.ema.length_after2 += result.length_after2;
  }
  return results;
}

void Leo::runEma(LayerNodes& layer)
{
  const std::vector<EmaResult> results = solveEma(layer);
  for (size_t index = 0; index < results.size(); ++index) {
    const EmaResult& result = results[index];
    if (result.arcs == 0 || result.moves == 0) {
      continue;  // only the subgraphs that changed are worth a line
    }
    logger_->report(
        "LEO EMA: layer={} subgraph={}/{} nodes={} arcs={} iterations={} "
        "status={} length={:.6f}->{:.6f}um",
        layer.layer,
        index + 1,
        results.size(),
        result.nodes,
        result.arcs,
        result.iterations,
        statusName(result.status),
        result.length_before2 / 2.0 / layer.dbu,
        result.length_after2 / 2.0 / layer.dbu);
  }
  logger_->report(
      "LEO EMA: layer={} subgraphs={} improved={} unchanged={} limited={} "
      "moves={} length_before={:.6f}um length_after={:.6f}um saved={:.6f}um",
      layer.layer,
      layer.ema.subgraphs,
      layer.ema.improved,
      layer.ema.unchanged,
      layer.ema.limited,
      layer.ema.moves,
      layer.ema.length_before2 / 2.0 / layer.dbu,
      layer.ema.length_after2 / 2.0 / layer.dbu,
      (layer.ema.length_before2 - layer.ema.length_after2) / 2.0 / layer.dbu);
}

}  // namespace leo
