// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <vector>

#include "boost/geometry/geometry.hpp"
#include "boost/geometry/index/rtree.hpp"
#include "delgpu/Graph.h"

namespace delgpu {
namespace {
namespace bg = boost::geometry;
namespace bgi = boost::geometry::index;

using RPoint = bg::model::point<int64_t, 2, bg::cs::cartesian>;
using RBox = bg::model::box<RPoint>;
using RTree = bgi::rtree<RBox, bgi::quadratic<8>>;

// Obstacle box in doubled DBU, so it compares exactly with the doubled trim
// centers.
RBox obstacleBox(const odb::Rect& rect)
{
  return RBox(RPoint(2 * int64_t{rect.xMin()}, 2 * int64_t{rect.yMin()}),
              RPoint(2 * int64_t{rect.xMax()}, 2 * int64_t{rect.yMax()}));
}

// Obstacle edges in the layer frame: along the preferred direction and across
// the tracks.
int64_t alongLow(const RBox& box, bool horizontal)
{
  return horizontal ? box.min_corner().get<0>() : box.min_corner().get<1>();
}

int64_t alongHigh(const RBox& box, bool horizontal)
{
  return horizontal ? box.max_corner().get<0>() : box.max_corner().get<1>();
}

int64_t acrossLow(const RBox& box, bool horizontal)
{
  return horizontal ? box.min_corner().get<1>() : box.min_corner().get<0>();
}

int64_t acrossHigh(const RBox& box, bool horizontal)
{
  return horizontal ? box.max_corner().get<1>() : box.max_corner().get<0>();
}

RBox queryBox(int64_t s_low,
              int64_t s_high,
              int64_t t_low,
              int64_t t_high,
              bool horizontal)
{
  return horizontal ? RBox(RPoint(s_low, t_low), RPoint(s_high, t_high))
                    : RBox(RPoint(t_low, s_low), RPoint(t_high, s_high));
}
}  // namespace

void computeDomains(LayerNodes& layer, const std::vector<odb::Rect>& obstacles)
{
  if (layer.nodes.empty()) {
    return;
  }
  const bool horizontal = layer.horizontal;
  RTree rtree;
  for (const odb::Rect& rect : obstacles) {
    if (rect.dx() > 0 && rect.dy() > 0) {
      rtree.insert(obstacleBox(rect));
    }
  }

  // The trim is W long along the preferred direction and 2W across, so the
  // doubled half extents are W and 2W.
  const int64_t half2 = layer.rules.width;
  const int64_t band2 = 2 * layer.rules.width;
  const int64_t cap2 = 2 * layer.rules.move_limit;

  std::vector<RBox> hits;
  for (Vnode& node : layer.nodes) {
    const int64_t s0 = along2(node, horizontal);
    const int64_t t0 = across2(node, horizontal);
    const int64_t box_low = s0 - half2;
    const int64_t box_high = s0 + half2;

    // Obstacles farther than the cap cannot limit the travel of the trim.
    hits.clear();
    rtree.query(bgi::intersects(queryBox(box_low - cap2,
                                         box_high + cap2,
                                         t0 - band2,
                                         t0 + band2,
                                         horizontal)),
                std::back_inserter(hits));

    // Start from the capped extremes and tighten them with the nearest
    // obstacle on each side of the trim.
    int64_t below = box_low - cap2;
    int64_t above = box_high + cap2;
    bool blocked = false;
    for (const RBox& hit : hits) {
      // Metal beside the trim, on another track, does not block it.
      if (acrossLow(hit, horizontal) >= t0 + band2
          || acrossHigh(hit, horizontal) <= t0 - band2) {
        continue;
      }
      const int64_t o_low = alongLow(hit, horizontal);
      const int64_t o_high = alongHigh(hit, horizontal);
      if (o_low < box_high && o_high > box_low) {
        blocked = true;  // the trim already overlaps this metal
      }
      if (o_high <= box_low) {
        below = std::max(below, o_high);
      }
      if (o_low >= box_high) {
        above = std::min(above, o_low);
      }
    }

    node.blocked = blocked;
    node.l_move2 = blocked ? 0 : box_low - below;
    node.u_move2 = blocked ? 0 : above - box_high;
    node.low2 = s0 - node.l_move2;
    node.up2 = s0 + node.u_move2;
  }
}

}  // namespace delgpu
