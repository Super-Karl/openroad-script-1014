// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "boost/polygon/polygon.hpp"
#include "delgpu/Trim.h"

namespace delgpu {
namespace {
namespace bp = boost::polygon;
using EndFaces
    = std::map<std::pair<int, bool>, std::vector<std::pair<int, int>>>;

int coordinate(int64_t value)
{
  if (value < std::numeric_limits<int>::min()
      || value > std::numeric_limits<int>::max()) {
    throw std::overflow_error("DEL trim coordinate overflow");
  }
  return static_cast<int>(value);
}

template <typename Ring>
void appendEnds(const Ring& ring,
                bool hole,
                int width,
                bool horizontal,
                const EndFaces& faces,
                const std::string& layer,
                unsigned int net_id,
                std::vector<Trim>& trims)
{
  std::vector<bp::point_data<int>> points(bp::begin_points(ring),
                                          bp::end_points(ring));
  if (points.size() < 4) {
    return;
  }
  long double area = 0;
  for (size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    area += static_cast<long double>(a.x()) * b.y()
            - static_cast<long double>(b.x()) * a.y();
  }
  // Outer rings contain metal; hole rings contain empty space.
  const int outward = ((area > 0) != hole) ? 1 : -1;
  for (size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    const int64_t dx = int64_t{b.x()} - a.x();
    const int64_t dy = int64_t{b.y()} - a.y();
    if ((horizontal && (dx != 0 || dy == 0))
        || (!horizontal && (dy != 0 || dx == 0))) {
      continue;
    }
    const bool positive = (horizontal ? dy : -dx) * outward > 0;
    const int64_t edge = horizontal ? a.x() : a.y();
    const int64_t low
        = horizontal ? std::min(a.y(), b.y()) : std::min(a.x(), b.x());
    const int64_t high
        = horizontal ? std::max(a.y(), b.y()) : std::max(a.x(), b.x());
    const auto face = faces.find({static_cast<int>(edge), positive});
    if (face == faces.end()) {
      continue;
    }
    // Clip to original preferred-direction end faces. A jog side is not a tip.
    std::vector<std::pair<int64_t, int64_t>> exposed;
    for (const auto& [lo, hi] : face->second) {
      const int64_t begin = std::max(low, int64_t{lo});
      const int64_t end = std::min(high, int64_t{hi});
      if (begin < end) {
        exposed.emplace_back(begin, end);
      }
    }
    std::sort(exposed.begin(), exposed.end());
    std::vector<std::pair<int64_t, int64_t>> merged;
    for (const auto& interval : exposed) {
      if (merged.empty() || interval.first > merged.back().second) {
        merged.push_back(interval);
      } else {
        merged.back().second = std::max(merged.back().second, interval.second);
      }
    }
    for (const auto& [begin, end] : merged) {
      const int64_t center = begin + (end - begin) / 2;
      const int s0 = coordinate(edge - (positive ? 0 : width));
      const int s1 = coordinate(edge + (positive ? width : 0));
      const int t0 = coordinate(center - width);
      const int t1 = coordinate(center + width);
      Trim trim;
      trim.box
          = horizontal ? odb::Rect(s0, t0, s1, t1) : odb::Rect(t0, s0, t1, s1);
      trim.layer = layer;
      trim.net_id = net_id;
      trim.high_end = positive;
      trims.push_back(trim);
    }
  }
}
}  // namespace

std::vector<Trim> makeTrimBoxes(const std::vector<odb::Rect>& metal,
                                int width,
                                bool horizontal,
                                std::string layer,
                                unsigned int net_id)
{
  if (width <= 0) {
    throw std::invalid_argument("DEL trim width must be positive");
  }
  bp::polygon_90_set_data<int> geometry;
  EndFaces faces;
  for (const auto& rect : metal) {
    if (rect.dx() > 0 && rect.dy() > 0) {
      if ((horizontal && rect.dx() >= rect.dy())
          || (!horizontal && rect.dy() >= rect.dx())) {
        const int low = horizontal ? rect.yMin() : rect.xMin();
        const int high = horizontal ? rect.yMax() : rect.xMax();
        faces[{horizontal ? rect.xMin() : rect.yMin(), false}].emplace_back(
            low, high);
        faces[{horizontal ? rect.xMax() : rect.yMax(), true}].emplace_back(
            low, high);
      }
      geometry.insert(bp::rectangle_data<int>(
          rect.xMin(), rect.yMin(), rect.xMax(), rect.yMax()));
    }
  }
  std::vector<bp::polygon_90_with_holes_data<int>> polygons;
  geometry.get(polygons);
  std::vector<Trim> trims;
  for (const auto& polygon : polygons) {
    appendEnds(polygon, false, width, horizontal, faces, layer, net_id, trims);
    for (auto it = bp::begin_holes(polygon); it != bp::end_holes(polygon);
         ++it) {
      appendEnds(*it, true, width, horizontal, faces, layer, net_id, trims);
    }
  }
  // Sort and deduplicate by geometry; the box decides the end it belongs to.
  std::sort(trims.begin(), trims.end(), [](const Trim& a, const Trim& b) {
    if (a.box != b.box) {
      return a.box < b.box;
    }
    return a.high_end < b.high_end;
  });
  trims.erase(std::unique(trims.begin(),
                          trims.end(),
                          [](const Trim& a, const Trim& b) {
                            return a.box == b.box && a.high_end == b.high_end;
                          }),
              trims.end());
  return trims;
}

}  // namespace delgpu
