// SPDX-License-Identifier: BSD-3-Clause
#include "del/Postprocess.h"
#include "gtest/gtest.h"

namespace del {
namespace {
LayerNodes layer(int count)
{
  LayerNodes l;
  l.layer = "M2";
  l.horizontal = true;
  l.rules = makeLayerRules(10);
  for (int i = 0; i < count; ++i) {
    Vnode n;
    n.id = i;
    n.high_end = true;
    n.up2 = 20;
    l.nodes.push_back(n);
  }
  return l;
}
void arc(LayerNodes& l,
         int s,
         int d,
         ArcType type,
         std::vector<ArcRange> ranges)
{
  Arc a;
  a.id = l.arcs.size();
  a.src = s;
  a.dst = d;
  a.type = type;
  a.ranges = std::move(ranges);
  l.arcs.push_back(a);
}
TEST(DelPostprocess, JointMergeAndExtentPolish)
{
  auto l = layer(2);
  l.nodes[1].center_x2 = l.nodes[1].low2 = 4;
  l.nodes[1].up2 = 24;
  arc(l, 0, 1, ArcType::kB, {{.high2 = 0}, {.low2 = 100, .has_high = false}});
  const auto r = postprocessLayer(l, {5, 3});
  EXPECT_TRUE(r.polish_optimal);
  EXPECT_DOUBLE_EQ(r.polished[0], 2);
  EXPECT_DOUBLE_EQ(r.polished[1], 0);
  EXPECT_EQ(r.legalized, (std::vector<double>{2, 0}));
  EXPECT_DOUBLE_EQ(trimResidualDbu(l, l.arcs[0], r.legalized), 0);
  EXPECT_EQ(r.nonoptimal_components, 0);
}
TEST(DelPostprocess, ReselectsBranchForShorterExtension)
{
  auto l = layer(2);
  l.nodes[0].blocked = true;
  arc(l,
      0,
      1,
      ArcType::kC,
      {{.has_low = false, .high2 = -6},
       {.high2 = 0},
       {.low2 = 6, .has_high = false}});
  const auto r = postprocessLayer(l, {0, 5});
  EXPECT_DOUBLE_EQ(r.polished[1], 3);
  EXPECT_EQ(r.legalized, (std::vector<double>{0, 0}));
}
TEST(DelPostprocess, ImpossibleResidualIsMinimizedAndStillCounted)
{
  auto l = layer(2);
  l.nodes[0].blocked = true;
  l.nodes[1].up2 = 8;
  arc(l, 0, 1, ArcType::kA, {{.low2 = 10, .has_high = false}});
  const auto r = postprocessLayer(l, {0, 2});
  EXPECT_EQ(r.legalized, (std::vector<double>{0, 4}));
  EXPECT_DOUBLE_EQ(trimResidualDbu(l, l.arcs[0], r.legalized), 1);
  EXPECT_EQ(r.nonoptimal_components, 0);
}
TEST(DelPostprocess, InconsistentCycleMinimizesCountThenResidual)
{
  auto l = layer(3);
  for (int i = 0; i < 3; ++i) {
    arc(l, i, (i + 1) % 3, ArcType::kA, {{.low2 = 2, .has_high = false}});
  }
  const auto r = postprocessLayer(l, {0, 0, 0});
  int count = 0;
  double residual = 0;
  for (const auto& a : l.arcs) {
    const double gap = trimResidualDbu(l, a, r.legalized);
    count += gap > 0;
    residual += gap;
  }
  EXPECT_EQ(count, 1);
  EXPECT_DOUBLE_EQ(residual, 3);
  EXPECT_DOUBLE_EQ(r.legalized[0] + r.legalized[1] + r.legalized[2], 3);
  EXPECT_EQ(r.nonoptimal_components, 0);
}
TEST(DelPostprocess, HalfDbuResidualRemainsExact)
{
  auto l = layer(2);
  l.nodes[0].blocked = true;
  l.nodes[1].center_x2 = l.nodes[1].low2 = l.nodes[1].up2 = 1;
  arc(l, 0, 1, ArcType::kB, {{.high2 = 0}});
  const auto r = postprocessLayer(l, {0, 0});
  EXPECT_DOUBLE_EQ(trimResidualDbu(l, l.arcs[0], r.legalized), 0.5);
}
TEST(DelPostprocess, SeparateComponentsUseLocalIndicesAndReportProgress)
{
  auto l = layer(4);
  l.nodes[0].blocked = l.nodes[1].blocked = true;
  arc(l, 0, 2, ArcType::kA, {{.low2 = 6, .has_high = false}});
  arc(l, 1, 3, ArcType::kA, {{.low2 = 10, .has_high = false}});
  PostprocessProgress progress;
  const auto r = postprocessLayer(l, {0, 0, 7, 9}, &progress);
  EXPECT_EQ(r.polished, (std::vector<double>{0, 0, 3, 5}));
  EXPECT_EQ(r.legalized, r.polished);
  EXPECT_EQ(progress.total.load(), 2);
  EXPECT_EQ(progress.completed.load(), 2);
}
TEST(DelPostprocess, CancellationStopsBeforeSolvingComponents)
{
  auto l = layer(2);
  PostprocessProgress progress;
  progress.cancelled = true;
  EXPECT_THROW(postprocessLayer(l, {0, 0}, &progress), std::runtime_error);
  EXPECT_EQ(progress.completed.load(), 0);
}
}  // namespace
}  // namespace del
