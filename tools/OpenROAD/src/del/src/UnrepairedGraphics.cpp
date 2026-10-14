// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "UnrepairedGraphics.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "del/Optimizer.h"

namespace del {
std::optional<odb::Rect> unrepairedArcBounds(
    const LayerNodes& layer,
    const Arc& arc,
    const std::vector<double>& displacement_dbu,
    double dbu_per_micron,
    bool exact)
{
  const auto& a = layer.nodes[arc.src];
  const auto& b = layer.nodes[arc.dst];
  const double width = layer.rules.width;
  const double d = (along2(b, layer.horizontal) - along2(a, layer.horizontal))
                       / (2 * width)
                   + displacement_dbu[arc.dst] / width
                   - displacement_dbu[arc.src] / width;
  double gap = std::numeric_limits<double>::infinity();
  for (const auto& range : arc.ranges) {
    double violation = 0;
    if (range.has_low) {
      violation = std::max(violation, range.low2 / (2 * width) - d);
    }
    if (range.has_high) {
      violation = std::max(violation, d - range.high2 / (2 * width));
    }
    gap = std::min(gap, violation);
  }
  if (exact) {
    const double distance2
        = along2(b, layer.horizontal) - along2(a, layer.horizontal)
          + 2 * (displacement_dbu[arc.dst] - displacement_dbu[arc.src]);
    double gap2 = std::numeric_limits<double>::infinity();
    for (const auto& r : arc.ranges) {
      gap2 = std::min(gap2,
                      std::max({0.0,
                                r.has_low ? r.low2 - distance2 : 0.0,
                                r.has_high ? distance2 - r.high2 : 0.0}));
    }
    gap = gap2 / (2 * width);
  }
  if (gap <= 0
      || (!exact
          && gap * width < kRepairToleranceNm * dbu_per_micron / 1000.0)) {
    return std::nullopt;
  }
  const double ax = layer.horizontal ? displacement_dbu[arc.src] : 0;
  const double ay = layer.horizontal ? 0 : displacement_dbu[arc.src];
  const double bx = layer.horizontal ? displacement_dbu[arc.dst] : 0;
  const double by = layer.horizontal ? 0 : displacement_dbu[arc.dst];
  return odb::Rect(
      static_cast<int>(
          std::floor(std::min(a.trim.xMin() + ax, b.trim.xMin() + bx))),
      static_cast<int>(
          std::floor(std::min(a.trim.yMin() + ay, b.trim.yMin() + by))),
      static_cast<int>(
          std::ceil(std::max(a.trim.xMax() + ax, b.trim.xMax() + bx))),
      static_cast<int>(
          std::ceil(std::max(a.trim.yMax() + ay, b.trim.yMax() + by))));
}
}  // namespace del
