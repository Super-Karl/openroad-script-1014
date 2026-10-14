// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <cmath>
#include <limits>

#include "delgpu/Optimizer.h"

namespace delgpu {
namespace {
constexpr int kStalledRounds = 3;
}  // namespace

bool LayerOptimizer::arcViolated(const Arc& arc,
                                 const std::vector<double>& positions) const
{
  const double d = arcDistance(arc, positions);
  double gap = std::numeric_limits<double>::infinity();
  for (const auto& range : arc.ranges) {
    double violation = 0;
    if (range.has_low) {
      violation = std::max(violation, range.low2 / (2 * width_) - d);
    }
    if (range.has_high) {
      violation = std::max(violation, d - range.high2 / (2 * width_));
    }
    gap = std::min(gap, violation);
  }
  return gap > 0 && gap * width_ >= repair_tolerance_dbu_;
}

bool LayerOptimizer::jumpAcrossBarrier(const Arc& arc, double& distance)
{
  const double d = arcDistance(arc, positions_);
  double midpoint;
  if (arc.type == ArcType::kB && d > 0 && d < layer_.rules.dt / width_) {
    midpoint = layer_.rules.dt / (2 * width_);
  } else if (arc.type == ArcType::kC && d != 0
             && std::abs(d) < layer_.rules.du / width_) {
    midpoint = std::copysign(layer_.rules.du / (2 * width_), d);
  } else {
    // A and the negative-distance B penalty are one-sided, without a peak.
    return false;
  }
  // At the peak there is no previous side: prefer increasing distance, then
  // try the other side if the pair cannot realize the first target.
  const double first_side = d <= midpoint ? 1 : -1;
  const int attempts = d == midpoint ? 2 : 1;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    const double side = attempt == 0 ? first_side : -first_side;
    const double target_distance = midpoint + side * jump_offset_;
    const double change = target_distance - d;
    const double direction = change > 0 ? 1.0 : -1.0;
    const double total = std::abs(change);
    const double src_capacity = direction > 0
                                    ? positions_[arc.src] - low_[arc.src]
                                    : up_[arc.src] - positions_[arc.src];
    const double dst_capacity = direction > 0
                                    ? up_[arc.dst] - positions_[arc.dst]
                                    : positions_[arc.dst] - low_[arc.dst];
    // Split by inverse resistance: a highly connected endpoint moves less.
    // src contributes a and dst contributes total-a, in opposite directions.
    const double min_src = std::max(0.0, total - dst_capacity);
    const double max_src = std::min(total, src_capacity);
    // At a joint domain boundary, subtraction can differ by a few ULPs.
    // Allow only roundoff here, then recheck the actual pair before committing.
    const double roundoff = 16 * std::numeric_limits<double>::epsilon()
                            * std::max({1.0,
                                        total,
                                        std::abs(d),
                                        std::abs(target_distance),
                                        std::abs(positions_[arc.src]),
                                        std::abs(positions_[arc.dst])});
    if (min_src > max_src + roundoff) {
      continue;
    }
    const double src_resistance = degree_[arc.src];
    const double dst_resistance = degree_[arc.dst];
    const double preferred_src
        = total * (dst_resistance / (src_resistance + dst_resistance));
    const double src_share = min_src > max_src
                                 ? max_src
                                 : std::clamp(preferred_src, min_src, max_src);
    const double dst_share = total - src_share;
    // Clamp only floating-point roundoff after checking joint feasibility.
    const double src = std::clamp(positions_[arc.src] - direction * src_share,
                                  low_[arc.src],
                                  up_[arc.src]);
    const double dst = std::clamp(positions_[arc.dst] + direction * dst_share,
                                  low_[arc.dst],
                                  up_[arc.dst]);
    const double initial = (along2(layer_.nodes[arc.dst], layer_.horizontal)
                            - along2(layer_.nodes[arc.src], layer_.horizontal))
                           / (2 * width_);
    const double after = initial + dst - src;
    if (!std::isfinite(src) || !std::isfinite(dst)
        || (after - midpoint) * side <= 0
        || std::abs(after - target_distance) > roundoff) {
      continue;
    }
    distance = std::max(std::abs(src - positions_[arc.src]),
                        std::abs(dst - positions_[arc.dst]))
               * width_;
    // Commit both endpoints together; never leave a partial failed jump.
    positions_[arc.src] = src;
    positions_[arc.dst] = dst;
    return true;
  }
  return false;
}

void LayerOptimizer::repairStalled(const std::vector<double>& previous,
                                   OptimizationStats& stats)
{
  // Match the GUI: it adds llround(displacement_dbu) to a fixed origin.
  // Compare accumulated displacements, not the size of this iteration's step:
  // a sub-DBU step can cross a rounding boundary and must reset the counter.
  const auto visibly_moved = [&](int node) {
    return std::llround(positions_[node] * width_)
           != std::llround(previous[node] * width_);
  };
  // Count complete stationary/violated iterations per arc, not per trim.
  for (size_t i = 0; i < layer_.arcs.size(); ++i) {
    const auto& arc = layer_.arcs[i];
    if (visibly_moved(arc.src) || visibly_moved(arc.dst)
        || !arcViolated(arc, previous) || !arcViolated(arc, positions_)) {
      stalled_rounds_[i] = 0;
    } else {
      stalled_rounds_[i] = std::min(kStalledRounds, stalled_rounds_[i] + 1);
    }
  }
  for (size_t i = 0; i < layer_.arcs.size(); ++i) {
    if (stalled_rounds_[i] < kStalledRounds) {
      continue;
    }
    ++stats.stall_triggered;
    const auto& arc = layer_.arcs[i];
    const double old_src = positions_[arc.src];
    const double old_dst = positions_[arc.dst];
    double distance = 0;
    if (!jumpAcrossBarrier(arc, distance)) {
      ++stats.stall_unresolved;
      continue;
    }
    // Historical field name retained for CSV compatibility: now counts jumps,
    // not successful legalization. Subsequent iterations descend from here.
    ++stats.stall_repaired;
    stats.max_repair_dbu = std::max(stats.max_repair_dbu, distance);
    // Reset incident arcs for each endpoint that actually moved, preventing
    // either endpoint from participating in another jump this iteration.
    for (const int node : {arc.src, arc.dst}) {
      const double old_position = node == arc.src ? old_src : old_dst;
      if (positions_[node] != old_position) {
        for (const size_t adjacent : incident_arcs_[node]) {
          stalled_rounds_[adjacent] = 0;
        }
      }
    }
  }
}
}  // namespace delgpu
