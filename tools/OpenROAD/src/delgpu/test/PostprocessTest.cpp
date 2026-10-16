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
TEST(DelgpuPostprocess, CooperativeRetractionUnlocksMergedChain)
{
  initializeGpu();
  auto l = graph({0, 0, 0, 0});
  for (int i = 1; i < 4; ++i) {
    edge(l, i - 1, i, {{.high2 = 0}}, ArcType::kB);
  }
  const std::vector<double> before{20, 20, 20, 20};
  auto g = makePostprocessGraph(l, 1000);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(before);
  auto single = gpu->postprocess(g, {0, 0, 8, false});
  EXPECT_EQ(single.retracted.extent, 80);
  EXPECT_EQ(gpu->displacementDbu(), before);
  gpu->setDisplacementDbu(before);
  auto cooperative = gpu->postprocess(g, {0, 0, 8, true});
  EXPECT_EQ(cooperative.retracted.extent, 0);
  EXPECT_EQ(cooperative.retracted.gap2, 0);
  EXPECT_EQ(cooperative.cooperative_moves, 4);
  equal(cooperative.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
  // Invalid warm starts must not alter the successful exact result.
  EXPECT_THROW(gpu->setDisplacementDbu({40, 0, 0, 0}), std::invalid_argument);
  EXPECT_EQ(gpu->displacementDbu(), (std::vector<double>{0, 0, 0, 0}));
}
TEST(DelgpuPostprocess,
     MergedGroupTranslationCanIncreaseOneEndpointToShortenTotal)
{
  initializeGpu();
  auto l = graph({0, 0, 100});
  l.nodes[2].low2 = -20;  // allow third trim to extend to displacement -60
  edge(l, 0, 1, {{.high2 = 0}}, ArcType::kB);
  edge(l, 1, 2, {{.high2 = 0}}, ArcType::kB);
  const std::vector<double> before{20, 20, -30};
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(before);
  const auto r
      = gpu->postprocess(makePostprocessGraph(l, 1000), {0, 0, 8, true});
  const auto after = gpu->displacementDbu();
  EXPECT_EQ(r.quantized.extent, 70);
  EXPECT_EQ(r.retracted.extent, 50);  // fixed-merge LP minimum
  EXPECT_GT(std::abs(after[2]), std::abs(before[2]));
  EXPECT_EQ(r.retracted.gap2, 0);
  EXPECT_EQ(r.group_moves, 3);
  equal(r.retracted, assessIntegerDisplacement(l, after, 1000));
}
TEST(DelgpuPostprocess, TruncatedPropagationRejectsOnlyUnsafeComponent)
{
  initializeGpu();
  auto l = graph(std::vector<int64_t>(202, 0));
  l.nodes[0].blocked = true;
  edge(l, 0, 1, {{.low2 = 40, .has_high = false}});
  for (int i = 2; i < 200; ++i) {
    edge(l, i - 1, i, {{.high2 = 0}}, ArcType::kB);
  }
  edge(l, 200, 201, {{.high2 = 0}}, ArcType::kB);
  std::vector<double> before(202, 20);
  before[0] = 0;
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(before);
  auto result
      = gpu->postprocess(makePostprocessGraph(l, 1000), {0, 0, 2, true, 4});
  auto after = gpu->displacementDbu();
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(after[i], before[i]);
  }
  EXPECT_EQ(after[200], 0);
  EXPECT_EQ(after[201], 0);
  EXPECT_EQ(result.retracted.extent, result.quantized.extent - 40);
  EXPECT_EQ(result.retracted.gap2, 0);
  equal(result.retracted, assessIntegerDisplacement(l, after, 1000));
}
TEST(DelgpuPostprocess, CooperativeMixedDirectionsPreserveEachOriginalResidual)
{
  initializeGpu();
  std::mt19937 random(3515);
  for (int sample = 0; sample < 15; ++sample) {
    auto l = graph(std::vector<int64_t>(20, 0));
    std::vector<double> before;
    for (int i = 0; i < 20; ++i) {
      before.push_back(int(random() % 41) - 20);
    }
    for (int i = 0; i < 45; ++i) {
      int a = random() % 20, b = random() % 20;
      if (a == b) {
        continue;
      }
      const int low = int(random() % 41) - 20;
      edge(l, a, b, {{.low2 = low, .high2 = low + 15}});
    }
    auto gpu = makeGpuOptimizer(l, 5, 0.25);
    gpu->setDisplacementDbu(before);
    auto r = gpu->postprocess(makePostprocessGraph(l, 1000), {0, 0, 8, true});
    auto after = gpu->displacementDbu();
    equal(r.retracted, assessIntegerDisplacement(l, after, 1000));
    EXPECT_LE(r.retracted.extent, r.quantized.extent);
    for (const auto& e : l.arcs) {
      auto one = l;
      one.arcs = {e};
      EXPECT_LE(assessIntegerDisplacement(one, after, 1000).gap2,
                assessIntegerDisplacement(one, before, 1000).gap2);
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
TEST(DelgpuPostprocess, JointNetworkOptimizesMixedTightChain)
{
  initializeGpu();
  auto l = graph({0, 0, 0});
  l.nodes[0].low2 = l.nodes[1].low2 = 0;
  l.nodes[2].up2 = 0;
  edge(l, 0, 1, {{.low2 = -4, .has_high = false}});
  edge(l, 1, 2, {{.low2 = -20, .has_high = false}});
  const std::vector<double> before{8, 6, -4};
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(before);
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 1;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 18);
  EXPECT_EQ(r.joint.extent, 0);
  equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, JointBranchesEscapeDisconnectedLegalIntervals)
{
  initializeGpu();
  auto l = graph({0, 0, 0});
  l.nodes[0].blocked = true;
  for (int i : {1, 2}) {
    l.nodes[i].low2 = 0;
  }
  edge(l, 0, 1, {{.low2 = 0, .high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 1, 2, {{.high2 = 0}}, ArcType::kB);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu({0, 10, 10});
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 1;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.quantized.extent, 20);
  EXPECT_EQ(r.joint.extent, 0);
  EXPECT_EQ(r.joint.gap2, 0);
  EXPECT_GT(r.joint_moves, 0);
  equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, JointNetworkMatchesExhaustiveIntegerOracle)
{
  initializeGpu();
  std::mt19937 random(20261016);
  for (int sample = 0; sample < 40; ++sample) {
    auto l = graph({0, 1, 0, 1});
    std::vector<double> before;
    for (int i = 0; i < 4; ++i) {
      const int sign = i % 2 ? -1 : 1;
      l.nodes[i].low2 = l.nodes[i].center_x2 + (sign > 0 ? 0 : -8);
      l.nodes[i].up2 = l.nodes[i].center_x2 + (sign > 0 ? 8 : 0);
      before.push_back(sign * int(random() % 5));
    }
    for (int i = 0; i < 4; ++i) {
      const int j = (i + 1) % 4;
      const int64_t d = l.nodes[j].center_x2 - l.nodes[i].center_x2
                        + 2 * (before[j] - before[i]);
      // Every branch includes a known feasible integer incumbent, including
      // half-DBU interval boundaries and nonzero lower/upper bounds.
      const int64_t low = d - random() % 5, up = d + random() % 5;
      edge(l, i, j, {{.low2 = low, .high2 = up}});
    }
    auto gpu = makeGpuOptimizer(l, 5, 0.25);
    gpu->setDisplacementDbu(before);
    PostprocessOptions options{0, 0, 0};
    options.joint_rounds = 1;
    const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
    int64_t minimum = INT64_MAX;
    for (int a = 0; a <= 4; ++a) {
      for (int b = -4; b <= 0; ++b) {
        for (int c = 0; c <= 4; ++c) {
          for (int d = -4; d <= 0; ++d) {
            const auto s = assessIntegerDisplacement(
                l, {double(a), double(b), double(c), double(d)}, 1000);
            if (!s.gap2) {
              minimum = std::min(minimum, s.extent);
            }
          }
        }
      }
    }
    EXPECT_EQ(r.joint.gap2, 0) << sample;
    EXPECT_EQ(r.joint.extent, minimum) << sample;
    equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
  }
}
TEST(DelgpuPostprocess, JointSeedsReselectSeveralIntervalsTogether)
{
  initializeGpu();
  auto l = graph({0, 0, 0});
  l.nodes[0].blocked = true;
  l.nodes[1].low2 = l.nodes[2].low2 = 0;
  edge(l, 0, 1, {{.high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 1, 2, {{.high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 0, 2, {{.high2 = 0}, {.low2 = 40, .high2 = 40}});
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu({0, 10, 20});
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 1;
  options.joint_trials = 4;  // only incumbent, strict and two whole-set seeds
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.joint.extent, 0);
  EXPECT_EQ(r.joint.gap2, 0);
  equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, JointIntervalEnumerationMatchesExhaustiveOracle)
{
  initializeGpu();
  std::mt19937 random(49681);
  for (int sample = 0; sample < 20; ++sample) {
    auto l = graph({0, 1, 0});
    std::vector<double> before;
    for (int i = 0; i < 3; ++i) {
      l.nodes[i].low2 = l.nodes[i].center_x2;
      l.nodes[i].up2 = l.nodes[i].center_x2 + 8;
      before.push_back(random() % 5);
    }
    for (int i = 0; i < 3; ++i) {
      const int j = (i + 1) % 3;
      const int64_t d = l.nodes[j].center_x2 - l.nodes[i].center_x2
                        + 2 * (before[j] - before[i]);
      const int64_t alt = int(random() % 17) - 8;
      edge(l,
           i,
           j,
           {{.low2 = d - 1, .high2 = d + 1}, {.low2 = alt, .high2 = alt + 1}});
    }
    auto gpu = makeGpuOptimizer(l, 5, 0.25);
    gpu->setDisplacementDbu(before);
    PostprocessOptions options{0, 0, 0};
    options.joint_rounds = 1;  // all 2^3 interval combinations fit in 16 trials
    const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
    int64_t minimum = INT64_MAX;
    for (int a = 0; a <= 4; ++a) {
      for (int b = 0; b <= 4; ++b) {
        for (int c = 0; c <= 4; ++c) {
          const auto s = assessIntegerDisplacement(
              l, {double(a), double(b), double(c)}, 1000);
          if (!s.gap2) {
            minimum = std::min(minimum, s.extent);
          }
        }
      }
    }
    EXPECT_EQ(r.joint.extent, minimum) << sample;
    EXPECT_EQ(r.joint.gap2, 0) << sample;
    equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
  }
}
TEST(DelgpuPostprocess, JointSizeBucketsAndOversizeFallback)
{
  initializeGpu();
  // Several components in one launch exercise all four resource buckets.
  auto l = graph(std::vector<int64_t>(12 + 24 + 48 + 96 + 140, 0));
  std::vector<double> before(l.nodes.size(), 10);
  int begin = 0;
  int64_t optimum = 0;
  for (int size : {12, 24, 48, 96, 140}) {
    l.nodes[begin].blocked = true;
    before[begin] = 0;
    for (int i = begin + 1; i < begin + size; ++i) {
      l.nodes[i].low2 = 0;
      if (i == begin + 1) {
        edge(l, begin, i, {{.low2 = 4, .has_high = false}});
      } else {
        edge(l, i - 1, i, {{.high2 = 0}}, ArcType::kB);
      }
    }
    optimum += (size - 1) * (size > 128 ? 10 : 2);
    begin += size;
  }
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(before);
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 1;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.joint.extent, optimum);
  EXPECT_EQ(r.joint_eligible, 4);
  EXPECT_EQ(r.joint_skipped, 1);
  equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, JointWarpDistancesPreserveLargeIntegerGeometry)
{
  initializeGpu();
  constexpr int64_t scale = int64_t{1} << 37;
  auto l = graph({0, 0, 0});
  l.nodes[0].blocked = true;
  for (int i : {1, 2}) {
    l.nodes[i].low2 = 0;
    l.nodes[i].up2 = int64_t{1} << 40;
  }
  edge(l,
       0,
       1,
       {{.low2 = 2 * scale, .high2 = 2 * scale},
        {.low2 = 4 * scale, .high2 = 4 * scale}});
  edge(l, 1, 2, {{.low2 = 2 * scale, .has_high = false}});
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu({0, double(2 * scale), double(3 * scale)});
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 1;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.joint.extent, 3 * scale);
  EXPECT_EQ(r.joint.gap2, 0);
  EXPECT_GT(r.joint_bound_solves, 0);
  equal(r.joint, assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, JointBranchesRejectInfeasibleCyclesAndPreserveResiduals)
{
  initializeGpu();
  auto l = graph({0, 0, 1, 0});
  for (int i : {0, 3}) {
    l.nodes[i].blocked = true;
  }
  l.nodes[1].low2 = 0;
  l.nodes[2].low2 = 1;
  edge(l, 0, 1, {{.low2 = 0, .high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 1, 3, {{.low2 = -20, .high2 = -20}});
  // The half-DBU center prevents exact integer merging on this parallel arc.
  edge(l, 1, 2, {{.low2 = 0, .high2 = 0}}, ArcType::kB);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  const std::vector<double> before{0, 10, 10, 0};
  gpu->setDisplacementDbu(before);
  PostprocessOptions options{0, 0, 0};
  options.joint_rounds = 3;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  const auto after = gpu->displacementDbu();
  equal(r.joint, assessIntegerDisplacement(l, after, 1000));
  EXPECT_LE(r.joint.extent, r.quantized.extent);
  EXPECT_LE(objective(r.joint), objective(r.quantized));
  for (const auto& e : l.arcs) {
    auto one = l;
    one.arcs = {e};
    EXPECT_LE(assessIntegerDisplacement(one, after, 1000).gap2,
              assessIntegerDisplacement(one, before, 1000).gap2);
  }
  options.joint_trials = 1;
  EXPECT_THROW(gpu->postprocess(makePostprocessGraph(l, 1000), options),
               std::invalid_argument);
}
TEST(DelgpuPostprocess, EmaMovesDirectionalTightChainWithoutSizeCutoff)
{
  initializeGpu();
  auto l = graph(std::vector<int64_t>(250, 0));
  for (auto& n : l.nodes) {
    n.low2 = 0;
  }
  for (int i = 1; i < 250; ++i) {
    edge(l, i - 1, i, {{.high2 = 0}}, ArcType::kB);
  }
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu(std::vector<double>(250, 20));
  PostprocessOptions options{0, 0, 8};
  options.ema_retract = true;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 0);
  EXPECT_EQ(r.retracted.gap2, 0);
  EXPECT_EQ(r.ema_moves, 1);
  EXPECT_EQ(r.ema_budget_reached, 0);
  equal(r.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, EmaJointMoveCanExtendMinorityToReduceTotal)
{
  initializeGpu();
  auto l = graph({0, 0, 100});
  l.nodes[0].low2 = l.nodes[1].low2 = 0;
  l.nodes[2].low2 = -20;
  l.nodes[2].up2 = 100;
  edge(l, 0, 1, {{.high2 = 0}}, ArcType::kB);
  edge(l, 1, 2, {{.high2 = 0}}, ArcType::kB);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  const std::vector<double> before{20, 20, -30};
  gpu->setDisplacementDbu(before);
  PostprocessOptions options{0, 0, 8};
  options.ema_retract = true;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 50);
  EXPECT_GT(std::abs(gpu->displacementDbu()[2]), 30);
  equal(r.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, EmaLegalIntervalJumpUnlocksPinnedTree)
{
  initializeGpu();
  auto l = graph({0, 0});
  l.nodes[0].blocked = true;
  l.nodes[1].low2 = 0;
  edge(l, 0, 1, {{.high2 = 0}, {.low2 = 20, .high2 = 20}}, ArcType::kB);
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  PostprocessOptions options{0, 0, 8};
  options.ema_retract = true;
  gpu->setDisplacementDbu({0, 10});
  auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 10);
  options.ema_jumps = true;
  gpu->setDisplacementDbu({0, 10});
  r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 0);
  EXPECT_GT(r.ema_jumps, 0);
  equal(r.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
TEST(DelgpuPostprocess, EmaMixedDomainsAndAllOriginalResidualsRemainSafe)
{
  initializeGpu();
  std::mt19937 random(92354);
  for (int sample = 0; sample < 10; ++sample) {
    auto l = graph(std::vector<int64_t>(25, 0));
    std::vector<double> before;
    for (int i = 0; i < 25; ++i) {
      before.push_back(int(random() % 41) - 20);
    }
    for (int i = 0; i < 50; ++i) {
      int a = random() % 25, b = random() % 25;
      if (a == b) {
        continue;
      }
      edge(l,
           a,
           b,
           {{.has_low = false, .high2 = -24},
            {.high2 = 0},
            {.low2 = 24, .has_high = false}},
           ArcType::kC);
    }
    auto gpu = makeGpuOptimizer(l, 5, 0.25);
    for (int mode : {0, 1, 2}) {
      gpu->setDisplacementDbu(before);
      PostprocessOptions options{0, 0, 8};
      options.ema_retract = true;
      options.ema_jumps = mode > 0;
      options.ema_seed = mode == 2;
      const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
      const auto after = gpu->displacementDbu();
      equal(r.retracted, assessIntegerDisplacement(l, after, 1000));
      EXPECT_LE(r.retracted.extent, r.quantized.extent);
      for (const auto& e : l.arcs) {
        auto one = l;
        one.arcs = {e};
        EXPECT_LE(assessIntegerDisplacement(one, after, 1000).gap2,
                  assessIntegerDisplacement(one, before, 1000).gap2);
      }
    }
  }
}
TEST(DelgpuPostprocess, EmaLpaSeedsChangeSeveralIntervalsTogether)
{
  initializeGpu();
  auto l = graph({0, 0, 0});
  l.nodes[0].blocked = true;
  l.nodes[1].low2 = l.nodes[2].low2 = 0;
  edge(l, 0, 1, {{.high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 1, 2, {{.high2 = 0}, {.low2 = 20, .high2 = 20}});
  edge(l, 0, 2, {{.high2 = 0}, {.low2 = 40, .high2 = 40}});
  auto gpu = makeGpuOptimizer(l, 5, 0.25);
  gpu->setDisplacementDbu({0, 10, 20});
  PostprocessOptions options{0, 0, 8};
  options.ema_retract = options.ema_jumps = options.ema_seed = true;
  const auto r = gpu->postprocess(makePostprocessGraph(l, 1000), options);
  EXPECT_EQ(r.retracted.extent, 0);
  EXPECT_EQ(r.retracted.gap2, 0);
  EXPECT_EQ(r.ema_seed_feasible, 1);
  EXPECT_EQ(r.ema_seed_accepted, 1);
  equal(r.retracted,
        assessIntegerDisplacement(l, gpu->displacementDbu(), 1000));
}
}  // namespace
}  // namespace delgpu
