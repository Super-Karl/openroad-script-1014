// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "delgpu/GpuOptimizer.h"
namespace delgpu {
std::unique_ptr<TrimOptimizer> makeGpuOptimizer(const LayerNodes& layer,
                                                double tolerance_dbu,
                                                double max_step,
                                                double dbu_per_micron,
                                                double jump_offset_nm)
{
  const double width = layer.rules.width;
  if (!std::isfinite(dbu_per_micron) || dbu_per_micron <= 0
      || !std::isfinite(jump_offset_nm) || jump_offset_nm <= 0) {
    throw std::invalid_argument("Invalid GPU jump scale or offset");
  }
  if (width <= 0
      || layer.nodes.size() + layer.arcs.size()
             >= static_cast<size_t>(std::numeric_limits<int>::max())
      || layer.arcs.size()
             >= static_cast<size_t>(std::numeric_limits<int>::max() / 3)) {
    throw std::invalid_argument("Invalid or oversized optimization graph");
  }
  std::vector<std::vector<int>> adjacency(layer.nodes.size());
  std::vector<OptEdge> edges;
  std::vector<OptRange> ranges;
  for (size_t i = 0; i < layer.arcs.size(); ++i) {
    const auto& e = layer.arcs[i];
    const double threshold = (e.type == ArcType::kA   ? layer.rules.d
                              : e.type == ArcType::kB ? layer.rules.dt
                                                      : layer.rules.du)
                             / width;
    OptEdge edge{e.src,
                 e.dst,
                 static_cast<int>(e.type),
                 static_cast<int>(ranges.size()),
                 0,
                 (along2(layer.nodes.at(e.dst), layer.horizontal)
                  - along2(layer.nodes.at(e.src), layer.horizontal))
                     / (2 * width),
                 threshold};
    if (e.src == e.dst || e.ranges.empty()) {
      throw std::invalid_argument("Invalid optimization arc");
    }
    for (const auto& range : e.ranges) {
      ranges.push_back({range.low2 / (2 * width),
                        range.high2 / (2 * width),
                        range.has_low,
                        range.has_high});
    }
    edge.range_end = static_cast<int>(ranges.size());
    edges.push_back(edge);
    adjacency.at(e.src).push_back(static_cast<int>(i));
    adjacency.at(e.dst).push_back(static_cast<int>(i));
  }
  std::vector<OptNode> nodes;
  std::vector<int> incident;
  for (size_t i = 0; i < layer.nodes.size(); ++i) {
    const auto& node = layer.nodes[i];
    const int64_t origin = along2(node, layer.horizontal);
    const double low = node.blocked ? 0 : (node.low2 - origin) / (2 * width);
    const double up = node.blocked ? 0 : (node.up2 - origin) / (2 * width);
    const int begin = static_cast<int>(incident.size());
    incident.insert(incident.end(), adjacency[i].begin(), adjacency[i].end());
    nodes.push_back({low, up, begin, static_cast<int>(incident.size())});
  }
  // Each color is a matching: its edges never write a shared endpoint.
  std::vector<int> colors(edges.size(), -1), marks(1, -1);
  int color_count = 0;
  for (size_t i = 0; i < edges.size(); ++i) {
    for (int node : {edges[i].src, edges[i].dst}) {
      for (int other : adjacency[node]) {
        if (colors[other] >= 0) {
          marks[colors[other]] = static_cast<int>(i);
        }
      }
    }
    int color = 0;
    while (color < color_count && marks[color] == static_cast<int>(i)) {
      ++color;
    }
    if (color == color_count) {
      ++color_count;
      marks.resize(color_count, -1);
    }
    colors[i] = color;
  }
  std::vector<int> offsets(color_count + 1, 0);
  for (int color : colors) {
    ++offsets[color + 1];
  }
  for (int i = 0; i < color_count; ++i) {
    offsets[i + 1] += offsets[i];
  }
  auto cursor = offsets;
  std::vector<int> order(edges.size());
  for (size_t i = 0; i < edges.size(); ++i) {
    order[cursor[colors[i]]++] = static_cast<int>(i);
  }
  return std::make_unique<TrimOptimizer>(
      nodes,
      edges,
      ranges,
      incident,
      order,
      offsets,
      width,
      tolerance_dbu,
      max_step,
      jump_offset_nm * dbu_per_micron / (1000.0 * width));
}

TrimStats assessDisplacement(const LayerNodes& layer,
                             const std::vector<double>& displacement,
                             double tolerance_dbu)
{
  if (displacement.size() != layer.nodes.size()) {
    throw std::runtime_error("Invalid position count");
  }
  TrimStats stats;
  constexpr double epsilon
      = 1e-6;  // DBU: numerical noise, far below manufacturing precision.
  for (size_t i = 0; i < displacement.size(); ++i) {
    const auto& n = layer.nodes[i];
    const double x = displacement[i];
    const double low
        = n.blocked ? 0 : (n.low2 - along2(n, layer.horizontal)) / 2.0;
    const double up
        = n.blocked ? 0 : (n.up2 - along2(n, layer.horizontal)) / 2.0;
    if (!std::isfinite(x) || x < low - epsilon || x > up + epsilon
        || (n.blocked && std::abs(x) > epsilon)) {
      throw std::runtime_error("DELGPU hard domain violated at node "
                               + std::to_string(i));
    }
    stats.extent_dbu += std::abs(x);
  }
  for (const auto& arc : layer.arcs) {
    const double width = layer.rules.width;
    const double distance = (along2(layer.nodes[arc.dst], layer.horizontal)
                             - along2(layer.nodes[arc.src], layer.horizontal))
                                / (2 * width)
                            + displacement[arc.dst] / width
                            - displacement[arc.src] / width;
    double gap = std::numeric_limits<double>::infinity();
    for (const auto& r : arc.ranges) {
      double violation = 0;
      if (r.has_low) {
        violation = std::max(violation, r.low2 / (2 * width) - distance);
      }
      if (r.has_high) {
        violation = std::max(violation, distance - r.high2 / (2 * width));
      }
      gap = std::min(gap, violation);
    }
    gap *= width;
    if (gap > epsilon) {
      ++stats.violations[static_cast<int>(arc.type)];
      if (gap + epsilon >= tolerance_dbu) {
        ++stats.remaining[static_cast<int>(arc.type)];
      }
      stats.max_violation_dbu = std::max(stats.max_violation_dbu, gap);
    }
  }
  return stats;
}
}  // namespace delgpu
