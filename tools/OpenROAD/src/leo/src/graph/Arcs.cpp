// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <vector>

#include "leo/Graph.h"

namespace leo {
namespace {

// Legal ranges of x_dst - x_src, in doubled DBU, for one arc type.
std::vector<ArcRange> rangesOf(ArcType type, const LayerRules& rules)
{
  switch (type) {
    case ArcType::kA:
      return {{.low2 = 2 * rules.d, .has_high = false}};
    case ArcType::kB:
      return {{.high2 = 0}, {.low2 = 2 * rules.dt, .has_high = false}};
    case ArcType::kC:
      return {{.has_low = false, .high2 = -2 * rules.du},
              {.high2 = 0},
              {.low2 = 2 * rules.du, .has_high = false}};
  }
  return {};
}

void addArc(LayerNodes& layer, ArcType type, const Vnode& src, const Vnode& dst)
{
  Arc arc;
  arc.src = src.id;
  arc.dst = dst.id;
  arc.type = type;
  arc.ranges = rangesOf(type, layer.rules);
  layer.arcs.push_back(arc);
}

// Same track: a pair needs an arc when one of the two nodes can travel far
// enough to break the separation rule. Two trims that face each other across a
// gap may merge into a single trim (B), any other pair must keep the metal
// apart (A).
void addSameTrackArcs(LayerNodes& layer, const std::vector<int>& order)
{
  const bool horizontal = layer.horizontal;
  const std::vector<Vnode>& nodes = layer.nodes;
  // A node can only approach by its own travel, at most move_limit each.
  const int64_t window2 = 2 * std::max(layer.rules.d, layer.rules.dt)
                          + 4 * layer.rules.move_limit;
  for (size_t i = 0; i < order.size(); ++i) {
    const Vnode& a = nodes[order[i]];
    for (size_t j = i + 1; j < order.size(); ++j) {
      const Vnode& b = nodes[order[j]];
      if (across2(b, horizontal) != across2(a, horizontal)) {
        break;  // the order groups one track at a time
      }
      if (along2(b, horizontal) - along2(a, horizontal) >= window2) {
        break;  // too far apart to ever reach the rule distance
      }
      const bool mergeable = a.high_end && !b.high_end;
      const ArcType type = mergeable ? ArcType::kB : ArcType::kA;
      // The farthest the two nodes can travel towards each other.
      const int64_t low2 = b.low2 - a.up2;
      const int64_t high2 = b.up2 - a.low2;
      if (coversRange(rangesOf(type, layer.rules), low2, high2)) {
        continue;  // every reachable distance is legal
      }
      addArc(layer, type, a, b);
    }
  }
}

// Adjacent tracks: two trims that can reach a distance shorter than Du along
// the track must either align (merge) or stay Du apart.
void addAdjacentTrackArcs(LayerNodes& layer, const std::vector<int>& order)
{
  if (layer.pitch <= 0) {
    return;
  }
  const bool horizontal = layer.horizontal;
  const std::vector<Vnode>& nodes = layer.nodes;
  const int64_t pitch2 = 2 * layer.pitch;
  const int64_t window2 = 2 * layer.rules.du + 4 * layer.rules.move_limit;
  const std::vector<ArcRange> ranges = rangesOf(ArcType::kC, layer.rules);
  for (size_t i = 0; i < order.size(); ++i) {
    const Vnode& a = nodes[order[i]];
    for (size_t j = i + 1; j < order.size(); ++j) {
      const Vnode& b = nodes[order[j]];
      const int64_t dt2 = across2(b, horizontal) - across2(a, horizontal);
      if (dt2 == 0) {
        continue;  // same track, handled by addSameTrackArcs
      }
      if (dt2 > pitch2) {
        break;  // farther than one track
      }
      if (std::abs(along2(b, horizontal) - along2(a, horizontal)) >= window2) {
        continue;  // too far apart to ever reach the rule distance
      }
      const int64_t low2 = b.low2 - a.up2;
      const int64_t high2 = b.up2 - a.low2;
      if (coversRange(ranges, low2, high2)) {
        continue;  // every reachable distance is legal
      }
      addArc(layer, ArcType::kC, a, b);
    }
  }
}

}  // namespace

ArcRange hullOf(const std::vector<ArcRange>& ranges)
{
  ArcRange hull;
  bool first = true;
  for (const ArcRange& range : ranges) {
    if (first) {
      hull = range;
      first = false;
      continue;
    }
    if (!range.has_low) {
      hull.has_low = false;
    } else if (hull.has_low) {
      hull.low2 = std::min(hull.low2, range.low2);
    }
    if (!range.has_high) {
      hull.has_high = false;
    } else if (hull.has_high) {
      hull.high2 = std::max(hull.high2, range.high2);
    }
  }
  return hull;
}

bool coversRange(const std::vector<ArcRange>& ranges, int64_t lo2, int64_t hi2)
{
  if (lo2 > hi2) {
    return true;  // no reachable distance at all
  }
  int64_t next = lo2;  // first value that is not covered yet
  for (const ArcRange& range : ranges) {
    if (range.has_high && range.high2 < next) {
      continue;
    }
    if (range.has_low && range.low2 > next) {
      return false;  // a gap between this range and the previous one
    }
    if (!range.has_high || range.high2 >= hi2) {
      return true;
    }
    next = range.high2 + 1;
  }
  return false;
}

const char* toString(ArcType type)
{
  switch (type) {
    case ArcType::kA:
      return "A";
    case ArcType::kB:
      return "B";
    case ArcType::kC:
      return "C";
  }
  return "?";
}

void buildArcs(LayerNodes& layer)
{
  layer.arcs.clear();
  if (layer.nodes.empty()) {
    return;
  }

  // Order by track first, then along the track, so the nodes of one track are
  // contiguous and sorted by position.
  const bool horizontal = layer.horizontal;
  const std::vector<Vnode>& nodes = layer.nodes;
  std::vector<int> order(nodes.size());
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(), [&](int a, int b) {
    const int64_t ta = across2(nodes[a], horizontal);
    const int64_t tb = across2(nodes[b], horizontal);
    if (ta != tb) {
      return ta < tb;
    }
    const int64_t sa = along2(nodes[a], horizontal);
    const int64_t sb = along2(nodes[b], horizontal);
    if (sa != sb) {
      return sa < sb;
    }
    return a < b;
  });

  addSameTrackArcs(layer, order);
  addAdjacentTrackArcs(layer, order);

  // Number the arcs by rule and endpoints so ids do not depend on the search.
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
  int id = 0;
  for (Arc& arc : layer.arcs) {
    arc.id = id++;
  }
}

}  // namespace leo
