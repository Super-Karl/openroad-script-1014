// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

#include "boost/geometry/geometry.hpp"
#include "boost/geometry/index/rtree.hpp"
#include "leo/Graph.h"

namespace leo {
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

void computeDomains(LayerNodes& layer,
                    const std::vector<odb::Rect>& obstacles,
                    bool full_domains)
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

    // Full-domain mode uses the native OpenDB coordinate range, without the
    // old arbitrary 20W cap. This includes every obstacle in the trim band.
    const int64_t search_low
        = full_domains ? 2 * int64_t{std::numeric_limits<int>::min()}
                       : box_low - cap2;
    const int64_t search_high
        = full_domains ? 2 * int64_t{std::numeric_limits<int>::max()}
                       : box_high + cap2;
    hits.clear();
    rtree.query(
        bgi::intersects(queryBox(
            search_low, search_high, t0 - band2, t0 + band2, horizontal)),
        std::back_inserter(hits));

    // Start from the capped extremes and tighten them with the nearest
    // obstacle on each side of the trim.
    int64_t below = search_low;
    int64_t above = search_high;
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

size_t pruneDomains(LayerNodes& layer, int prune_factor)
{
  if (prune_factor <= 0 || layer.rules.dt <= 0
      || layer.rules.dt
             > std::numeric_limits<int64_t>::max() / 2 / prune_factor) {
    throw std::invalid_argument(
        "LEO prune_factor and Dt must be positive and P representable");
  }
  const int64_t p2 = 2 * int64_t{prune_factor} * layer.rules.dt;
  size_t clipped = 0;
  int64_t max_travel2 = 0;
  for (Vnode& node : layer.nodes) {
    if (node.up2 - node.low2 > p2) {
      if (node.high_end) {
        node.up2 = node.low2 + p2;
      } else {
        node.low2 = node.up2 - p2;
      }
      ++clipped;
    }
    const int64_t original = along2(node, layer.horizontal);
    node.l_move2 = original - node.low2;
    node.u_move2 = node.up2 - original;
    max_travel2 = std::max({max_travel2, node.l_move2, node.u_move2});
  }
  // Search windows must cover the actual retained domains, including k>4.
  layer.rules.move_limit = (max_travel2 + 1) / 2;
  return clipped;
}

}  // namespace leo
