// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "leo/Lpa.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "../ComponentModel.h"
#include "DifferenceConstraints.h"
#include "leo/Graph.h"
#include "leo/Leo.h"
#include "utl/Logger.h"

namespace leo {
namespace {

// Weight of a node in the objective: a trim at the high end of the metal adds
// metal when it moves up, one at the low end when it moves down.
int weightOf(const Vnode& node)
{
  return node.high_end ? 1 : -1;
}

// Position with the smallest extension: the low domain end for a node that
// adds metal when it moves up, the high domain end otherwise.
int64_t idealOf(const Vnode& node)
{
  return weightOf(node) > 0 ? node.low2 : node.up2;
}

// Distance from delta_ideal to a range, zero when it lies inside.
int64_t rangeDistance(const ArcRange& range, int64_t delta_ideal)
{
  if (range.has_low && delta_ideal < range.low2) {
    return range.low2 - delta_ideal;
  }
  if (range.has_high && delta_ideal > range.high2) {
    return delta_ideal - range.high2;
  }
  return 0;
}

// Constraints of one range: x_dst - x_src in [low, high].
void constraintsOf(const ArcRange& range,
                   int src,
                   int dst,
                   std::vector<Constraint>& out)
{
  out.clear();
  if (range.has_low) {
    out.push_back({src, dst, range.low2});
  }
  if (range.has_high) {
    out.push_back({dst, src, -range.high2});
  }
}

// Slack of an arc in the relaxation model, the smaller the tighter.
int64_t slackOf(const Arc& arc,
                const std::vector<int64_t>& lower,
                const std::vector<int64_t>& upper)
{
  int64_t slack = std::numeric_limits<int64_t>::max();
  for (const ArcRange& range : arc.ranges) {
    if (range.has_low) {
      slack = std::min(slack, upper[arc.dst] - lower[arc.src] - range.low2);
    }
    if (range.has_high) {
      slack = std::min(slack, upper[arc.src] - lower[arc.dst] + range.high2);
    }
  }
  return slack;
}

const char* statusName(SubgraphResult::Status status)
{
  switch (status) {
    case SubgraphResult::Status::kSolved:
      return "solved";
    case SubgraphResult::Status::kPartial:
      return "partial";
    case SubgraphResult::Status::kInvalidDomain:
      return "invalid_domain";
  }
  return "?";
}

}  // namespace

std::vector<SubgraphResult> solveLpa(LayerNodes& layer)
{
  std::vector<SubgraphResult> results;
  layer.lpa = LpaStats{};
  const size_t node_count = layer.nodes.size();
  if (node_count == 0) {
    return results;
  }

  layer.lpa.subgraphs = layer.components.size();
  results.resize(layer.components.size());
  size_t index = 0;
  for (const Component& component : layer.components) {
    SubgraphResult& result = results[index];
    ++index;
    result.nodes = component.nodes.size();
    result.arcs = component.arcs.size();
    LayerNodes model = componentModel(layer, component);
    const size_t count = model.nodes.size();
    std::vector<int64_t> low(count), up(count), ideal(count);
    bool invalid = false;
    for (size_t id = 0; id < count; ++id) {
      const Vnode& node = model.nodes[id];
      low[id] = node.low2;
      up[id] = node.up2;
      ideal[id] = idealOf(node);
      invalid = invalid || node.low2 > node.up2;
    }

    if (component.arcs.empty()) {
      continue;  // nothing to solve, the node stays where it is
    }
    layer.lpa.with_arcs++;

    if (invalid) {
      ++layer.lpa.invalid;
      result.status = SubgraphResult::Status::kInvalidDomain;
      continue;
    }

    // Relaxation model for the order of the arcs: the hull of every interval.
    // A relaxed subgraph falls back to the domains for the ordering.
    std::vector<Constraint> hulls;
    hulls.reserve(2 * component.arcs.size());
    for (const Arc& arc : model.arcs) {
      const ArcRange hull = hullOf(arc.ranges);
      if (hull.has_low) {
        hulls.push_back({arc.src, arc.dst, hull.low2});
      }
      if (hull.has_high) {
        hulls.push_back({arc.dst, arc.src, -hull.high2});
      }
    }
    const FeasibleBounds relaxed = solveDifferenceConstraints(low, up, hulls);
    const auto& relax_lower = relaxed.feasible ? relaxed.lower : low;
    const auto& relax_upper = relaxed.feasible ? relaxed.upper : up;

    // Initial slack fixes the order once, before accepting any range.
    std::vector<std::pair<int64_t, int>> arc_keys;
    for (const Arc& arc : model.arcs) {
      arc_keys.emplace_back(slackOf(arc, relax_lower, relax_upper), arc.id);
    }
    std::sort(
        arc_keys.begin(), arc_keys.end(), [&](const auto& a, const auto& b) {
          const Arc& arc_a = model.arcs[a.second];
          const Arc& arc_b = model.arcs[b.second];
          if (arc_a.type != arc_b.type) {
            return arc_a.type < arc_b.type;
          }
          if (a.first != b.first) {
            return a.first < b.first;
          }
          return a.second < b.second;
        });

    // Greedy: accept for every arc the feasible range closest to the ideal
    // relative position of its two nodes. Every trial solves all previously
    // accepted constraints plus the candidate from scratch.
    std::vector<Constraint> accepted;
    size_t unresolved = 0;
    for (const auto& [slack, arc_id] : arc_keys) {
      Arc& arc = model.arcs[arc_id];
      const int64_t delta_ideal = ideal[arc.dst] - ideal[arc.src];
      std::vector<int> range_order(arc.ranges.size());
      std::iota(range_order.begin(), range_order.end(), 0);
      std::sort(range_order.begin(), range_order.end(), [&](int a, int b) {
        const int64_t distance_a = rangeDistance(arc.ranges[a], delta_ideal);
        const int64_t distance_b = rangeDistance(arc.ranges[b], delta_ideal);
        return distance_a != distance_b ? distance_a < distance_b : a < b;
      });
      arc.selected_range = -1;
      for (int range : range_order) {
        std::vector<Constraint> constraints;
        constraintsOf(arc.ranges[range], arc.src, arc.dst, constraints);
        std::vector<Constraint> trial = accepted;
        trial.insert(trial.end(), constraints.begin(), constraints.end());
        if (solveDifferenceConstraints(low, up, trial).feasible) {
          accepted = std::move(trial);
          arc.selected_range = range;
          break;
        }
      }
      if (arc.selected_range < 0) {
        ++unresolved;
      }
    }
    result.unresolved_arcs = unresolved;

    // Direct oracle solution, without ideal-coordinate repair or fallback.
    const FeasibleBounds final = solveDifferenceConstraints(low, up, accepted);
    if (!final.feasible) {
      throw std::logic_error("LEO accepted constraints lost feasibility");
    }
    const std::vector<int64_t>& positions = final.lower;
    for (size_t i = 0; i < model.arcs.size(); ++i) {
      layer.arcs[component.arcs[i]].selected_range
          = model.arcs[i].selected_range;
    }
    int64_t added_length2 = 0;
    size_t moved = 0;
    for (size_t id = 0; id < count; ++id) {
      Vnode& node = layer.nodes[component.nodes[id]];
      const int64_t position = positions[id];
      const int64_t offset = position - along2(node, layer.horizontal);
      added_length2 += weightOf(node) * offset;
      if (layer.horizontal) {
        node.pos_x2 = position;
        node.pos_y2 = node.center_y2;
      } else {
        node.pos_x2 = node.center_x2;
        node.pos_y2 = position;
      }
      moved += offset != 0 ? 1 : 0;
    }

    result.moved = moved;
    result.added_length2 = added_length2;
    result.status = unresolved == 0 ? SubgraphResult::Status::kSolved
                                    : SubgraphResult::Status::kPartial;
    layer.lpa.unresolved_arcs += unresolved;
    layer.lpa.added_length2 += added_length2;
    if (unresolved == 0) {
      ++layer.lpa.solved;
    } else {
      ++layer.lpa.partial;
    }
  }
  return results;
}

void Leo::runLpa(LayerNodes& layer)
{
  const std::vector<SubgraphResult> results = solveLpa(layer);
  size_t arc_free = 0;
  for (size_t index = 0; index < results.size(); ++index) {
    const SubgraphResult& result = results[index];
    if (result.arcs == 0) {
      ++arc_free;
      continue;
    }
    logger_->report(
        "LEO LPA: layer={} subgraph={}/{} nodes={} arcs={} status={} "
        "moved={} added_length={:.6f}um",
        layer.layer,
        index + 1,
        results.size(),
        result.nodes,
        result.arcs,
        statusName(result.status),
        result.moved,
        result.added_length2 / 2.0 / layer.dbu);
  }
  size_t moved_total = 0;
  for (const Vnode& node : layer.nodes) {
    moved_total += moved(node) ? 1 : 0;
  }
  logger_->report(
      "LEO LPA: layer={} subgraphs={} solved={} partial={} invalid={} "
      "arc_free={} unresolved_arcs={} moved={} added_length={:.6f}um",
      layer.layer,
      layer.lpa.subgraphs,
      layer.lpa.solved,
      layer.lpa.partial,
      layer.lpa.invalid,
      arc_free,
      layer.lpa.unresolved_arcs,
      moved_total,
      layer.lpa.added_length2 / 2.0 / layer.dbu);
}

}  // namespace leo
