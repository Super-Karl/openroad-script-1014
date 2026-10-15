// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "delgpu/GpuOptimizer.h"

namespace delgpu {
namespace {
int64_t floorHalf(int64_t v)
{
  return v / 2 - (v < 0 && v % 2 != 0);
}
int64_t ceilHalf(int64_t v)
{
  return v / 2 + (v > 0 && v % 2 != 0);
}
// Bound all intermediate distances and accumulated scores before device use.
void validateMagnitude(const LayerNodes& layer)
{
  int64_t magnitude = 1;
  const auto include = [&](int64_t v) {
    constexpr int64_t limit = int64_t{1} << 40;
    if (v < -limit || v > limit) {
      throw std::invalid_argument(
          "Postprocess geometry exceeds exact integer bounds");
    }
    magnitude = std::max(magnitude, std::abs(v));
  };
  for (const auto& n : layer.nodes) {
    include(along2(n, layer.horizontal));
    include(n.low2);
    include(n.up2);
  }
  for (const auto& e : layer.arcs) {
    if (static_cast<int>(e.type) < 0 || static_cast<int>(e.type) > 2
        || e.src < 0 || e.dst < 0 || e.src == e.dst
        || static_cast<size_t>(e.src) >= layer.nodes.size()
        || static_cast<size_t>(e.dst) >= layer.nodes.size()
        || e.ranges.empty()) {
      throw std::invalid_argument("Invalid postprocess arc");
    }
    for (const auto& r : e.ranges) {
      if (r.has_low) {
        include(r.low2);
      }
      if (r.has_high) {
        include(r.high2);
      }
      if (r.has_low && r.has_high && r.low2 > r.high2) {
        throw std::invalid_argument("Empty postprocess interval");
      }
    }
  }
  if (static_cast<__int128>(magnitude) * 32
          * (layer.nodes.size() + layer.arcs.size() + 1)
      > std::numeric_limits<int64_t>::max()) {
    throw std::invalid_argument("Postprocess score may overflow");
  }
}
}  // namespace

PostprocessGraph makePostprocessGraph(const LayerNodes& layer, int dbu)
{
  if (dbu <= 0
      || layer.nodes.size() + layer.arcs.size()
             >= static_cast<size_t>(std::numeric_limits<int>::max() / 3)) {
    throw std::invalid_argument("Invalid postprocess units or graph size");
  }
  validateMagnitude(layer);
  PostprocessGraph g;
  // gap2*1000 >= 10*dbu, without multiplying a potentially large residual.
  g.tolerance2 = (int64_t{10} * dbu + 999) / 1000;
  for (const auto& n : layer.nodes) {
    const auto origin = along2(n, layer.horizontal);
    const auto low = n.blocked ? 0 : ceilHalf(n.low2 - origin);
    const auto up = n.blocked ? 0 : floorHalf(n.up2 - origin);
    if (low > 0 || up < 0 || low > up) {
      throw std::invalid_argument("Invalid postprocess movement domain");
    }
    g.nodes.push_back({origin, low, up});
  }
  std::vector<std::vector<int>> neighbors(g.nodes.size());
  for (const auto& e : layer.arcs) {
    neighbors[e.src].push_back(e.dst);
    neighbors[e.dst].push_back(e.src);
    g.initial2.push_back(g.nodes[e.dst].origin2 - g.nodes[e.src].origin2);
    for (const auto& r : e.ranges) {
      g.ranges.push_back({r.low2, r.high2, r.has_low, r.has_high});
    }
  }
  // Deterministic greedy coloring, including isolated nodes for retraction.
  std::vector<int> colors(g.nodes.size(), -1), marked;
  int count = 0;
  for (size_t i = 0; i < neighbors.size(); ++i) {
    marked.resize(count + 1, -1);
    for (int j : neighbors[i]) {
      if (colors[j] >= 0) {
        marked[colors[j]] = static_cast<int>(i);
      }
    }
    int c = 0;
    while (marked[c] == static_cast<int>(i)) {
      ++c;
    }
    colors[i] = c;
    count = std::max(count, c + 1);
  }
  g.color_offsets.assign(count + 1, 0);
  for (int c : colors) {
    ++g.color_offsets[c + 1];
  }
  for (int c = 0; c < count; ++c) {
    g.color_offsets[c + 1] += g.color_offsets[c];
  }
  auto cursor = g.color_offsets;
  g.color_nodes.resize(g.nodes.size());
  for (size_t i = 0; i < colors.size(); ++i) {
    g.color_nodes[cursor[colors[i]]++] = static_cast<int>(i);
  }
  return g;
}

PostprocessStats assessIntegerDisplacement(
    const LayerNodes& layer,
    const std::vector<double>& displacement,
    int dbu)
{
  // Check directly against original Vnodes/ArcRanges, independently of device
  // packed arrays, floating residuals and candidate generation.
  validateMagnitude(layer);
  if (dbu <= 0 || displacement.size() != layer.nodes.size()) {
    throw std::invalid_argument("Invalid integer result size or units");
  }
  std::vector<int64_t> x;
  PostprocessStats s;
  for (size_t i = 0; i < displacement.size(); ++i) {
    const double v = displacement[i];
    if (!std::isfinite(v) || std::abs(v) > (int64_t{1} << 42)
        || v != std::round(v)) {
      throw std::runtime_error("Noninteger postprocess displacement");
    }
    const int64_t d = static_cast<int64_t>(v);
    const auto& n = layer.nodes[i];
    const int64_t p2 = along2(n, layer.horizontal) + 2 * d;
    if ((n.blocked && d != 0) || (!n.blocked && (p2 < n.low2 || p2 > n.up2))) {
      throw std::runtime_error("Postprocess hard domain violated");
    }
    x.push_back(d);
    s.extent += std::abs(d);
  }
  const int64_t threshold = (int64_t{10} * dbu + 999) / 1000;
  for (const auto& e : layer.arcs) {
    const int64_t d = along2(layer.nodes[e.dst], layer.horizontal)
                      - along2(layer.nodes[e.src], layer.horizontal)
                      + 2 * (x[e.dst] - x[e.src]);
    int64_t gap = std::numeric_limits<int64_t>::max();
    for (const auto& r : e.ranges) {
      int64_t g = 0;
      if (r.has_low) {
        g = std::max(g, r.low2 - d);
      }
      if (r.has_high) {
        g = std::max(g, d - r.high2);
      }
      gap = std::min(gap, g);
    }
    const auto type = static_cast<int>(e.type);
    s.violations[type] += gap > 0;
    s.remaining[type] += gap >= threshold;
    s.gap2 += gap;
    s.max_gap2 = std::max(s.max_gap2, gap);
  }
  return s;
}
}  // namespace delgpu
