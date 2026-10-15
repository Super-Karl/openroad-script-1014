// SPDX-License-Identifier: BSD-3-Clause
#include "gtest/gtest.h"
#include "leo/Graph.h"
#include "leo/Measurement.h"

namespace leo {
namespace {
LayerNodes pair(int64_t distance2)
{
  LayerNodes layer;
  Vnode a, b;
  a.low2 = a.up2 = 0;
  b.center_x2 = b.pos_x2 = b.low2 = b.up2 = distance2;
  layer.nodes = {a, b};
  Arc arc;
  arc.src = 0;
  arc.dst = 1;
  arc.type = ArcType::kA;
  arc.ranges = {{.low2 = 100, .has_high = false}};
  layer.arcs = {arc};
  return layer;
}
TEST(LeoMeasurement, CountsHalfDbuAndBelowFiveNmUnselectedViolations)
{
  for (int64_t distance : {100, 99, 91, 90, 89}) {
    const auto q = assessLayer(pair(distance), 1000);
    EXPECT_EQ(q.violations, distance < 100 ? 1u : 0u);
    EXPECT_EQ(q.remaining, distance <= 90 ? 1u : 0u);
    EXPECT_EQ(q.by_type[0], q.violations);
    EXPECT_EQ(q.unselected, 1u);
    EXPECT_EQ(q.max_gap2, 100 - distance);
  }
}
TEST(LeoMeasurement, ChecksEveryTypeAndTheUnionOfLegalIntervals)
{
  auto layer = pair(40);
  Arc b = layer.arcs[0], c = b;
  b.type = ArcType::kB;
  b.ranges = {{.high2 = 0}, {.low2 = 100, .has_high = false}};
  c.type = ArcType::kC;
  c.ranges = {{.has_low = false, .high2 = -60},
              {.high2 = 0},
              {.low2 = 60, .has_high = false}};
  layer.arcs.push_back(b);
  layer.arcs.push_back(c);
  const auto q = assessLayer(layer, 1000);
  EXPECT_EQ(q.violations, 3u);
  EXPECT_EQ(q.remaining, 3u);
  EXPECT_EQ(q.by_type, (std::array<size_t, 3>{1, 1, 1}));
}
TEST(LeoMeasurement, SelectedRangeFailureIsSeparateFromGraphViolation)
{
  auto layer = pair(100);
  auto& arc = layer.arcs[0];
  arc.type = ArcType::kB;
  arc.ranges = {{.high2 = 0}, {.low2 = 100, .has_high = false}};
  arc.selected_range = 0;
  const auto q = assessLayer(layer, 1000);
  EXPECT_EQ(q.violations, 0u);
  EXPECT_EQ(q.selected_violations, 1u);
}
}  // namespace
}  // namespace leo
