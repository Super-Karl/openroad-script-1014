// SPDX-License-Identifier: BSD-3-Clause
#include "delgpu/GraphGpu.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>

#include "delgpu/GraphCuda.h"

namespace delgpu {
namespace {
int checkedCount(size_t count)
{
  if (count > static_cast<size_t>(std::numeric_limits<int>::max())) {
    throw std::overflow_error("DELGPU index exceeds INT_MAX");
  }
  return static_cast<int>(count);
}

int buildBvh(std::vector<DeviceBox>& boxes,
             std::vector<DeviceBvh>& tree,
             int begin,
             int end)
{
  DeviceBox bounds = boxes[begin];
  for (int i = begin + 1; i < end; ++i) {
    bounds.sl = std::min(bounds.sl, boxes[i].sl);
    bounds.sh = std::max(bounds.sh, boxes[i].sh);
    bounds.tl = std::min(bounds.tl, boxes[i].tl);
    bounds.th = std::max(bounds.th, boxes[i].th);
  }
  const int id = checkedCount(tree.size());
  tree.push_back({bounds, -1, -1, begin, end});
  if (end - begin > 8) {
    const int middle = begin + (end - begin) / 2;
    const bool split_s = bounds.sh - bounds.sl >= bounds.th - bounds.tl;
    std::nth_element(boxes.begin() + begin,
                     boxes.begin() + middle,
                     boxes.begin() + end,
                     [split_s](const DeviceBox& a, const DeviceBox& b) {
                       return split_s ? a.sl + a.sh < b.sl + b.sh
                                      : a.tl + a.th < b.tl + b.th;
                     });
    const int left = buildBvh(boxes, tree, begin, middle);
    const int right = buildBvh(boxes, tree, middle, end);
    tree[id].left = left;
    tree[id].right = right;
  }
  return id;
}

std::vector<ArcRange> ranges(ArcType type, const LayerRules& r)
{
  switch (type) {
    case ArcType::kA:
      return {{.low2 = 2 * r.d, .has_high = false}};
    case ArcType::kB:
      return {{.high2 = 0}, {.low2 = 2 * r.dt, .has_high = false}};
    case ArcType::kC:
      return {{.has_low = false, .high2 = -2 * r.du},
              {.high2 = 0},
              {.low2 = 2 * r.du, .has_high = false}};
  }
  throw std::runtime_error("DELGPU invalid arc type");
}
}  // namespace

void buildGraphGpu(LayerNodes& layer, const std::vector<odb::Rect>& obstacles)
{
  layer.arcs.clear();
  if (layer.nodes.empty()) {
    return;
  }
  const int n = checkedCount(layer.nodes.size());
  std::vector<DeviceNode> nodes;
  nodes.reserve(n);
  for (const auto& node : layer.nodes) {
    nodes.push_back({along2(node, layer.horizontal),
                     across2(node, layer.horizontal),
                     0,
                     0,
                     node.high_end,
                     0});
  }
  std::vector<DeviceBox> boxes;
  boxes.reserve(obstacles.size());
  for (const auto& box : obstacles) {
    if (box.dx() <= 0 || box.dy() <= 0) {
      continue;
    }
    const int64_t xl = 2 * int64_t{box.xMin()};
    const int64_t xh = 2 * int64_t{box.xMax()};
    const int64_t yl = 2 * int64_t{box.yMin()};
    const int64_t yh = 2 * int64_t{box.yMax()};
    boxes.push_back(layer.horizontal ? DeviceBox{xl, xh, yl, yh}
                                     : DeviceBox{yl, yh, xl, xh});
  }
  const int box_count = checkedCount(boxes.size());
  std::vector<DeviceBvh> tree;
  if (!boxes.empty()) {
    buildBvh(boxes, tree, 0, box_count);
  }
  std::vector<int> order(n);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    if (nodes[a].t != nodes[b].t) {
      return nodes[a].t < nodes[b].t;
    }
    if (nodes[a].s != nodes[b].s) {
      return nodes[a].s < nodes[b].s;
    }
    return a < b;
  });
  std::vector<DeviceTrack> tracks;
  for (int begin = 0; begin < n;) {
    int end = begin + 1;
    while (end < n && nodes[order[end]].t == nodes[order[begin]].t) {
      ++end;
    }
    tracks.push_back({nodes[order[begin]].t, begin, end});
    begin = end;
  }
  const auto& r = layer.rules;
  DeviceRules rules{r.width, r.d, r.dt, r.du, r.move_limit, layer.pitch};
  DeviceEdge* raw = nullptr;
  size_t edge_count = 0;
  char error[512]{};
  const int status = delgpuBuildCuda(nodes.data(),
                                     n,
                                     boxes.data(),
                                     box_count,
                                     tree.data(),
                                     checkedCount(tree.size()),
                                     order.data(),
                                     tracks.data(),
                                     checkedCount(tracks.size()),
                                     rules,
                                     &raw,
                                     &edge_count,
                                     error,
                                     sizeof(error));
  std::unique_ptr<DeviceEdge, decltype(&delgpuFreeEdges)> edges(
      raw, delgpuFreeEdges);
  if (status != 0) {
    throw std::runtime_error(std::string("DELGPU CUDA graph: ") + error);
  }
  checkedCount(edge_count);
  for (int i = 0; i < n; ++i) {
    auto& node = layer.nodes[i];
    node.low2 = nodes[i].low;
    node.up2 = nodes[i].up;
    node.l_move2 = nodes[i].s - nodes[i].low;
    node.u_move2 = nodes[i].up - nodes[i].s;
    node.blocked = nodes[i].blocked;
  }
  layer.arcs.reserve(edge_count);
  for (size_t i = 0; i < edge_count; ++i) {
    Arc arc;
    arc.src = raw[i].src;
    arc.dst = raw[i].dst;
    arc.type = static_cast<ArcType>(raw[i].type);
    arc.ranges = ranges(arc.type, layer.rules);
    layer.arcs.push_back(std::move(arc));
  }
  std::sort(
      layer.arcs.begin(), layer.arcs.end(), [](const Arc& a, const Arc& b) {
        if (a.type != b.type) {
          return a.type < b.type;
        }
        if (a.src != b.src) {
          return a.src < b.src;
        }
        return a.dst < b.dst;
      });
  for (size_t i = 0; i < layer.arcs.size(); ++i) {
    layer.arcs[i].id = static_cast<int>(i);
  }
}

void verifyGraph(const LayerNodes& gpu, const LayerNodes& cpu)
{
  auto mismatch = [&](const std::string& detail) {
    throw std::runtime_error("DELGPU CPU/GPU mismatch: layer=" + gpu.layer + " "
                             + detail);
  };
  if (gpu.nodes.size() != cpu.nodes.size()) {
    mismatch("node count");
  }
  for (size_t i = 0; i < gpu.nodes.size(); ++i) {
    const auto& a = gpu.nodes[i];
    const auto& b = cpu.nodes[i];
    if (a.id != b.id || a.trim_index != b.trim_index || a.net_id != b.net_id
        || a.trim != b.trim || a.high_end != b.high_end
        || a.center_x2 != b.center_x2 || a.center_y2 != b.center_y2
        || a.low2 != b.low2 || a.up2 != b.up2 || a.blocked != b.blocked
        || a.l_move2 != b.l_move2 || a.u_move2 != b.u_move2) {
      mismatch("node " + std::to_string(i));
    }
  }
  if (gpu.arcs.size() != cpu.arcs.size()) {
    mismatch("arc count");
  }
  for (size_t i = 0; i < gpu.arcs.size(); ++i) {
    const auto& a = gpu.arcs[i];
    const auto& b = cpu.arcs[i];
    if (a.id != b.id || a.type != b.type || a.src != b.src || a.dst != b.dst
        || a.ranges.size() != b.ranges.size()) {
      mismatch("arc " + std::to_string(i));
    }
    for (size_t j = 0; j < a.ranges.size(); ++j) {
      const auto& ar = a.ranges[j];
      const auto& br = b.ranges[j];
      if (ar.has_low != br.has_low || ar.low2 != br.low2
          || ar.has_high != br.has_high || ar.high2 != br.high2) {
        mismatch("arc range " + std::to_string(i));
      }
    }
  }
}
}  // namespace delgpu
