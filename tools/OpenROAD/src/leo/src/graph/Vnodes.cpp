// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "leo/Graph.h"

namespace leo {
namespace {

// Exact integer square root; std::sqrt alone can round up across an integer
// boundary for large values.
int64_t integerSqrt(int64_t value)
{
  int64_t root = static_cast<int64_t>(std::sqrt(static_cast<double>(value)));
  while (root > 0 && root * root > value) {
    --root;
  }
  while ((root + 1) * (root + 1) <= value) {
    ++root;
  }
  return root;
}

}  // namespace

std::vector<Vnode> makeVnodes(const std::vector<Trim>& trims,
                              size_t begin,
                              size_t end,
                              bool horizontal)
{
  std::vector<Vnode> nodes;
  nodes.reserve(end - begin);
  for (size_t index = begin; index < end; ++index) {
    const odb::Rect& box = trims[index].box;
    Vnode node;
    node.trim_index = index;
    node.net_id = trims[index].net_id;
    node.trim = box;
    node.high_end = trims[index].high_end;
    node.center_x2 = static_cast<int64_t>(box.xMin()) + box.xMax();
    node.center_y2 = static_cast<int64_t>(box.yMin()) + box.yMax();
    node.pos_x2 = node.center_x2;
    node.pos_y2 = node.center_y2;
    nodes.push_back(node);
  }

  // Sort along the preferred direction so ids do not depend on the net order of
  // the database, and so neighbors of a node are close in id space.
  const auto primary = [horizontal](const Vnode& node) {
    return horizontal ? node.center_x2 : node.center_y2;
  };
  const auto secondary = [horizontal](const Vnode& node) {
    return horizontal ? node.center_y2 : node.center_x2;
  };
  std::sort(nodes.begin(), nodes.end(), [&](const Vnode& a, const Vnode& b) {
    if (primary(a) != primary(b)) {
      return primary(a) < primary(b);
    }
    if (secondary(a) != secondary(b)) {
      return secondary(a) < secondary(b);
    }
    if (a.net_id != b.net_id) {
      return a.net_id < b.net_id;
    }
    return a.trim_index < b.trim_index;
  });

  int id = 0;
  for (Vnode& node : nodes) {
    node.id = id++;
  }
  return nodes;
}

CenterOffset centerOffset(const Vnode& a, const Vnode& b, bool horizontal)
{
  const int64_t dx2 = b.center_x2 - a.center_x2;
  const int64_t dy2 = b.center_y2 - a.center_y2;
  if (horizontal) {
    return CenterOffset{.ds2 = dx2, .dt2 = dy2};
  }
  return CenterOffset{.ds2 = dy2, .dt2 = dx2};
}

int64_t centerDistance2(const Vnode& a, const Vnode& b)
{
  const int64_t dx2 = b.center_x2 - a.center_x2;
  const int64_t dy2 = b.center_y2 - a.center_y2;
  return integerSqrt(dx2 * dx2 + dy2 * dy2);
}

double centerDistance(const Vnode& a, const Vnode& b)
{
  return centerDistance2(a, b) / 2.0;
}

}  // namespace leo
