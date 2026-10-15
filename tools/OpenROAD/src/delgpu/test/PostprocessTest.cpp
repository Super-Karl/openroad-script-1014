// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <vector>

#include "delgpu/Gpu.h"
#include "delgpu/GpuOptimizer.h"
#include "gtest/gtest.h"

namespace delgpu {
namespace {
LayerNodes graph(const std::vector<int64_t>& centers)
{
  LayerNodes l;
  l.rules = makeLayerRules(10);
  for (auto center : centers) {
    Vnode n;
    n.id = l.nodes.size();
    n.center_x2 = center;
    n.low2 = center - 60;
    n.up2 = center + 60;
    l.nodes.push_back(n);
  }
  return l;
}
void edge(LayerNodes& l,
          int s,
          int t,
          std::vector<ArcRange> ranges,
          ArcType type = ArcType::kA)
{
  Arc e;
  e.id = l.arcs.size();
  e.src = s;
  e.dst = t;
  e.type = type;
  e.ranges = std::move(ranges);
  l.arcs.push_back(e);
}
std::array<int64_t, 4> objective(const PostprocessStats& s)
{
  return {s.remaining[0] + s.remaining[1] + s.remaining[2],
          s.violations[0] + s.violations[1] + s.violations[2],
          s.gap2,
          s.extent};
}
void equal(const PostprocessStats& a, const PostprocessStats& b)
{
  EXPECT_EQ(objective(a), objective(b));
  EXPECT_EQ(a.max_gap2, b.max_gap2);
  for (int t = 0; t < 3; ++t) {
    EXPECT_EQ(a.remaining[t], b.remaining[t]);
    EXPECT_EQ(a.violations[t], b.violations[t]);
  }
}
TEST(DelgpuPostprocess, NegativeHalfDomainsAndHalfCentersRemainExact)
{
  initializeGpu();
  auto l = graph({1, 22});
  l.nodes[0].low2 = -2;  // displacement low=-1.5 -> -1
  l.nodes[0].up2 = 2;    // displacement up=0.5 -> 0
  l.nodes[1].low2 = 21;  // displacement low=-0.5 -> 0
  l.nodes[1].up2 = 25;   // displacement up=1.5 -> 1
  edge(l, 0, 1, {{.low2 = 22, .has_high = false}});
  const auto g = makePostprocessGraph(l, 1000);
  EXPECT_EQ(g.nodes[0].low, -1);
  EXPECT_EQ(g.nodes[0].up, 0);
  EXPECT_EQ(g.nodes[1].low, 0);
  EXPECT_EQ(g.nodes[1].up, 1);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  auto result = gpu->postprocess(g);
  equal(result.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
  EXPECT_EQ(result.retracted.violations[0], 0);
}
TEST(DelgpuPostprocess, IntegerFiveNmThresholdIncludesExactBoundary)
{
  auto l = graph({0, 1, 2});
  edge(l, 0, 1, {{.low2 = 11, .has_high = false}});
  edge(l, 0, 2, {{.low2 = 11, .has_high = false}});
  auto s = assessIntegerDisplacement(l, {0, 0, 0}, 1000);
  EXPECT_EQ(s.violations[0], 2);
  EXPECT_EQ(s.remaining[0], 1);  // doubled gaps 10 and 9: exactly 5nm vs 4.5nm
  s = assessIntegerDisplacement(l, {0, 0, 0}, 1001);
  EXPECT_EQ(s.remaining[0], 0);
  EXPECT_THROW(assessIntegerDisplacement(l, {0, 0.5, 0}, 1000),
               std::runtime_error);
}
TEST(DelgpuPostprocess, PairEscapesSingleNodeLocalMinimumAndDeduplicatesEdges)
{
  initializeGpu();
  auto l = graph({0, 0, 0, 0});
  // A,B can both shift by one; fixed anchors permit positions 0 or 1 only.
  // AB wants B-A=1. Changing either alone would fix AB but break its anchor,
  // preserving counts/gap and increasing extent, so singles must reject it.
  l.nodes[0].low2 = 0;
  l.nodes[0].up2 = 2;
  l.nodes[1].low2 = 0;
  l.nodes[1].up2 = 4;
  for (int i : {2, 3}) {
    l.nodes[i].blocked = true;
  }
  edge(l, 0, 1, {{.low2 = 2, .high2 = 2}});
  edge(l, 2, 0, {{.low2 = 0, .high2 = 0}, {.low2 = 2, .high2 = 2}});
  edge(l, 3, 1, {{.low2 = 0, .high2 = 0}, {.low2 = 4, .high2 = 4}});
  // Legal only when A=1, B=2. From 0,0 single B=1 fixes AB but breaks its
  // anchor.
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  auto r = gpu->postprocess(makePostprocessGraph(l, 1000), {8, 4, 4});
  EXPECT_GT(r.pair_moves, 0);
  EXPECT_EQ(r.retracted.violations[0], 0);
  equal(r.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, SharedNeighborsAndParallelArcsNeverWorsenObjective)
{
  initializeGpu();
  std::mt19937 random(20261015);
  for (int sample = 0; sample < 12; ++sample) {
    auto l = graph(std::vector<int64_t>(30, 0));
    for (int i = 0; i < 30; ++i) {
      auto& n = l.nodes[i];
      n.center_x2 = 7 * i + (i % 2);
      n.low2 = n.center_x2 - 21;
      n.up2 = n.center_x2 + 27;
      n.blocked = i % 11 == 0;
    }
    for (int i = 0; i < 65; ++i) {
      int a = random() % 30, b = random() % 30;
      if (a == b) {
        continue;
      }
      if (a > b) {
        std::swap(a, b);
      }
      const int type = random() % 3;
      if (type == 0) {
        edge(l, a, b, {{.low2 = 20, .has_high = false}});
      } else if (type == 1) {
        edge(l,
             a,
             b,
             {{.high2 = 0}, {.low2 = 30, .has_high = false}},
             ArcType::kB);
      } else {
        edge(l,
             a,
             b,
             {{.has_low = false, .high2 = -24},
              {.high2 = 0},
              {.low2 = 24, .has_high = false}},
             ArcType::kC);
      }
    }
    auto g = makePostprocessGraph(l, 1000);
    std::vector<int> colors(l.nodes.size());
    for (size_t c = 0; c + 1 < g.color_offsets.size(); ++c) {
      for (int k = g.color_offsets[c]; k < g.color_offsets[c + 1]; ++k) {
        colors[g.color_nodes[k]] = c;
      }
    }
    for (const auto& e : l.arcs) {
      EXPECT_NE(colors[e.src], colors[e.dst]);
    }
    auto gpu = makeGpuOptimizer(l, 5, 0.25);
    for (int i = 0; i < 3; ++i) {
      gpu->step(10);
    }
    auto rounded = gpu->displacementDbu();
    for (size_t i = 0; i < rounded.size(); ++i) {
      rounded[i] = std::clamp<int64_t>(
          std::llround(rounded[i]), g.nodes[i].low, g.nodes[i].up);
    }
    auto r = gpu->postprocess(g, {12, 4, 0});
    equal(r.quantized, assessIntegerDisplacement(l, rounded, 1000));
    auto repaired = gpu->displacementDbu();
    equal(r.repaired, assessIntegerDisplacement(l, repaired, 1000));
    EXPECT_LE(objective(r.repaired), objective(r.quantized));
    auto rr = gpu->postprocess(g, {0, 0, 8});
    auto retracted = gpu->displacementDbu();
    equal(rr.retracted, assessIntegerDisplacement(l, retracted, 1000));
    EXPECT_LE(objective(rr.retracted), objective(rr.quantized));
    // Check the stronger retraction guarantee separately for every edge.
    for (const auto& e : l.arcs) {
      auto one = l;
      one.arcs = {e};
      EXPECT_LE(assessIntegerDisplacement(one, retracted, 1000).gap2,
                assessIntegerDisplacement(one, repaired, 1000).gap2);
    }
  }
}
TEST(DelgpuPostprocess, BlockedUnsatisfiableHalfDbuMergeAndEmptyEdges)
{
  initializeGpu();
  auto l = graph({0, 1});
  for (auto& n : l.nodes) {
    n.blocked = true;
  }
  edge(l, 0, 1, {{.low2 = 0, .high2 = 0}}, ArcType::kB);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  auto r = gpu->postprocess(makePostprocessGraph(l, 1000));
  EXPECT_EQ(r.retracted.violations[1],
            1);  // odd initial2 cannot merge by integer shifts
  EXPECT_EQ(gpu->displacementDbu(), (std::vector<double>{0, 0}));
  l.arcs.clear();
  gpu = makeGpuOptimizer(l, 5, 0.25);
  EXPECT_EQ(gpu->postprocess(makePostprocessGraph(l, 1000)).retracted.gap2, 0);
  l.nodes[0].low2 = INT64_MIN;
  EXPECT_THROW(makePostprocessGraph(l, 1000), std::invalid_argument);
}
}  // namespace
}  // namespace delgpu
