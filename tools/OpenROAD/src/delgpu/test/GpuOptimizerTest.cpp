// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include <cmath>

#include "delgpu/Gpu.h"
#include "delgpu/GpuOptimizer.h"
#include "delgpu/Optimizer.h"
#include "gtest/gtest.h"
namespace delgpu {
namespace {
unsigned long long count(const TrimStats& s)
{
  return s.violations[0] + s.violations[1] + s.violations[2];
}
LayerNodes pair(ArcType type, int64_t distance2, bool blocked = false)
{
  LayerNodes layer;
  layer.rules = makeLayerRules(10);
  layer.horizontal = true;
  Vnode a;
  a.id = 0;
  a.low2 = blocked ? 0 : -400;
  a.up2 = blocked ? 0 : 400;
  a.blocked = blocked;
  Vnode b = a;
  b.id = 1;
  b.center_x2 = distance2;
  b.low2 += distance2;
  b.up2 += distance2;
  layer.nodes = {a, b};
  Arc e;
  e.src = 0;
  e.dst = 1;
  e.type = type;
  if (type == ArcType::kA) {
    e.ranges = {{.low2 = 100, .has_high = false}};
  } else if (type == ArcType::kB) {
    e.ranges = {{.high2 = 0}, {.low2 = 100, .has_high = false}};
  } else {
    e.ranges = {{.has_low = false, .high2 = -60},
                {.high2 = 0},
                {.low2 = 60, .has_high = false}};
  }
  layer.arcs = {e};
  return layer;
}
TEST(DelgpuOptimizer, FirstGradientStepMatchesCpu)
{
  initializeGpu();
  auto layer = pair(ArcType::kA, 80);
  LayerOptimizer cpu(layer, 5, 1000, 5, 0.2);
  auto gpu = makeGpuOptimizer(layer, 5, 0.2);
  EXPECT_NEAR(gpu->evaluate(10).energy, cpu.evaluate(10).energy, 1e-10);
  auto gs = gpu->step(10);
  auto cs = cpu.step(10);
  auto gx = gpu->displacementDbu();
  auto cx = cpu.displacementDbu();
  EXPECT_NEAR(gs.energy, cs.energy, 1e-10);
  for (size_t i = 0; i < gx.size(); ++i) {
    EXPECT_NEAR(gx[i], cx[i], 1e-10);
  }
}
TEST(DelgpuOptimizer, MidpointAndNegativeDistanceFallBelowFiveNm)
{
  initializeGpu();
  for (auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
    for (int sign : {-1, 1}) {
      auto layer = pair(type, sign * (type == ArcType::kC ? 30 : 50));
      auto gpu = makeGpuOptimizer(layer, 5, 0.2);
      for (int i = 0; i < 100 && count(gpu->evaluate(80)) > 0; ++i) {
        gpu->step(80);
      }
      auto stats = gpu->evaluate(80);
      const auto assessed
          = assessDisplacement(layer, gpu->displacementDbu(), 5);
      EXPECT_LT(stats.max_violation_dbu, 5);
      EXPECT_LT(assessed.max_violation_dbu, 5);
      for (int type = 0; type < 3; ++type) {
        EXPECT_EQ(stats.remaining[type], 0u);
        EXPECT_EQ(assessed.remaining[type], 0u);
      }
    }
  }
}
TEST(DelgpuOptimizer, BlockedPairRemainsStationaryAndReportsConflict)
{
  initializeGpu();
  auto layer = pair(ArcType::kA, 50, true);
  auto gpu = makeGpuOptimizer(layer, 5, 0.2);
  for (int i = 0; i < 12; ++i) {
    gpu->step(10);
  }
  auto stats = gpu->evaluate(10);
  auto x = gpu->displacementDbu();
  EXPECT_EQ(count(stats), 1u);
  EXPECT_DOUBLE_EQ(x[0], 0);
  EXPECT_DOUBLE_EQ(x[1], 0);
  EXPECT_NO_THROW(assessDisplacement(layer, x, 5));
}
TEST(DelgpuOptimizer, SharedEndpointsStayInDomainAtLastIteration)
{
  initializeGpu();
  auto layer = pair(ArcType::kA, 30);
  for (int i = 2; i < 50; ++i) {
    auto node = layer.nodes[1];
    node.id = i;
    node.center_x2 = 30 * i;
    node.low2 = node.center_x2 - 400;
    node.up2 = node.center_x2 + 400;
    if (i % 7 == 0) {
      node.blocked = true;
      node.low2 = node.up2 = node.center_x2;
    }
    layer.nodes.push_back(node);
    Arc e = layer.arcs[0];
    e.id = i - 1;
    e.src = i - 1;
    e.dst = i;
    layer.arcs.push_back(e);
    if (i % 3 == 0) {
      e.id = layer.arcs.size();
      e.src = i - 2;
      layer.arcs.push_back(e);
    }
  }
  auto gpu = makeGpuOptimizer(layer, 5, 0.2);
  const auto initial = gpu->evaluate(10);
  for (int i = 0; i < 60; ++i) {
    auto stats = gpu->step(80);
    EXPECT_TRUE(std::isfinite(stats.energy));
    EXPECT_NO_THROW(assessDisplacement(layer, gpu->displacementDbu(), 5));
  }
  const auto final = gpu->evaluate(80);
  const auto checked = assessDisplacement(layer, gpu->displacementDbu(), 5);
  EXPECT_LE(count(final), count(initial));
  EXPECT_EQ(count(final), count(checked));
}
TEST(DelgpuOptimizer, IsolatedNodeAndInvalidStep)
{
  initializeGpu();
  auto layer = pair(ArcType::kA, 80);
  layer.nodes.resize(1);
  layer.arcs.clear();
  auto gpu = makeGpuOptimizer(layer, 5, 0.2);
  EXPECT_EQ(count(gpu->step(10)), 0u);
  EXPECT_DOUBLE_EQ(gpu->displacementDbu()[0], 0);
  EXPECT_THROW(makeGpuOptimizer(layer, 5, 0), std::invalid_argument);
}
TEST(DelgpuOptimizer, CustomIntervalsRetainCpuBarrierSemantics)
{
  initializeGpu();
  auto layer = pair(ArcType::kB, 50);
  layer.arcs[0].ranges = {{.low2 = 70, .high2 = 80}};
  LayerOptimizer cpu(layer, 5, 1000, 35, 0.2);
  auto gpu = makeGpuOptimizer(layer, 5, 0.2, 1000, 35, 19);
  for (int i = 0; i < 60; ++i) {
    const auto cs = cpu.step(80);
    const auto gs = gpu->step(80);
    EXPECT_EQ(gs.repaired, cs.stall_repaired);
    EXPECT_EQ(gs.remaining[1], cs.remaining_violations[1]);
    EXPECT_NEAR(gs.energy, cs.energy, 1e-8);
  }
  const auto final = gpu->evaluate(80);
  const auto checked = assessDisplacement(layer, gpu->displacementDbu(), 5);
  EXPECT_EQ(count(final), count(checked));
  EXPECT_EQ(final.remaining[1], checked.remaining[1]);
  // Arbitrary legal intervals control residuals, not the CPU potential peak.
  // Do not project into custom intervals as the previous GPU repair did.
  EXPECT_GT(final.remaining[1], 0u);
}
TEST(DelgpuOptimizer,
     IndependentComponentsPreserveCanonicalJumpAndImmediateReset)
{
  initializeGpu();
  auto layer = pair(ArcType::kB, 40);
  layer.nodes.clear();
  layer.arcs.clear();
  for (int component = 0; component < 3; ++component) {
    auto l = pair(ArcType::kB, 40);
    l.nodes[0].up2 = 0;
    l.nodes[1].low2 = 40;
    for (auto node : l.nodes) {
      node.id += 2 * component;
      node.center_x2 += 1000 * component;
      node.low2 += 1000 * component;
      node.up2 += 1000 * component;
      layer.nodes.push_back(node);
    }
    auto arc = l.arcs[0];
    arc.src += 2 * component;
    arc.dst += 2 * component;
    // The second copy must be skipped after the first jump resets its counter.
    layer.arcs.push_back(arc);
    layer.arcs.push_back(arc);
  }
  LayerOptimizer cpu(layer, 5, 1000, 1, 0.25);
  auto gpu = makeGpuOptimizer(layer, 5, 0.25, 1000, 1, 19);
  for (int iteration = 1; iteration <= 6; ++iteration) {
    const auto cs = cpu.step(10);
    const auto gs = gpu->step(10);
    EXPECT_EQ(gs.repaired, cs.stall_repaired);
    if (iteration <= 3) {
      EXPECT_EQ(gs.repaired, iteration == 3 ? 3u : 0u);
    }
    const auto cx = cpu.displacementDbu(), gx = gpu->displacementDbu();
    for (size_t n = 0; n < cx.size(); ++n) {
      EXPECT_NEAR(cx[n], gx[n], 1e-9);
    }
  }
}
TEST(DelgpuOptimizer, MixedSharedGraphMatchesCpuForTwoHundredIterations)
{
  initializeGpu();
  auto layer = pair(ArcType::kB, 40);
  layer.nodes.clear();
  layer.arcs.clear();
  for (int i = 0; i < 30; ++i) {
    auto node = pair(ArcType::kA, 0).nodes[0];
    node.id = i;
    node.center_x2 = 30 * i;
    node.low2 += node.center_x2;
    node.up2 += node.center_x2;
    node.blocked = i % 7 == 0;
    if (node.blocked) {
      node.low2 = node.up2 = node.center_x2;
    }
    layer.nodes.push_back(node);
    if (i) {
      auto arc = pair(static_cast<ArcType>(i % 3), 0).arcs[0];
      arc.src = i - 1;
      arc.dst = i;
      layer.arcs.push_back(arc);
      if (i % 3 == 0) {
        arc.src = i - 2;
        layer.arcs.push_back(arc);
      }
    }
  }
  LayerOptimizer cpu(layer, 5, 1000, 35, 0.25);
  auto gpu = makeGpuOptimizer(layer, 5, 0.25, 1000, 35, 19);
  for (int i = 1; i <= 200; ++i) {
    SCOPED_TRACE(i);
    const double lambda = 10 * (1 << std::min(3, (i - 1) / 25));
    const auto cs = cpu.step(lambda);
    const auto gs = gpu->step(lambda);
    EXPECT_EQ(gs.repaired, cs.stall_repaired);
    for (int type = 0; type < 3; ++type) {
      EXPECT_EQ(gs.remaining[type], cs.remaining_violations[type]);
    }
    EXPECT_NEAR(gs.energy, cs.energy, 1e-8 * std::max(1.0, cs.energy));
    const auto cx = cpu.displacementDbu(), gx = gpu->displacementDbu();
    for (size_t n = 0; n < cx.size(); ++n) {
      EXPECT_NEAR(cx[n], gx[n], 1e-7);
    }
    EXPECT_NO_THROW(assessDisplacement(layer, gx, 5));
  }
}
TEST(DelgpuOptimizer, DisabledBacktrackingKeepsEnergyGuardAndHardDomains)
{
  initializeGpu();
  for (auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
    auto layer = pair(type, 40);
    auto gpu = makeGpuOptimizer(layer, 5, 0.25, 1000, 35, 0);
    for (int i = 0; i < 40; ++i) {
      const auto before = gpu->evaluate(80);
      const auto after = gpu->step(80);
      EXPECT_LE(after.candidates, 3);
      if (!after.repaired) {
        EXPECT_LE(after.energy, before.energy + 1e-10);
      }
      EXPECT_NO_THROW(assessDisplacement(layer, gpu->displacementDbu(), 5));
    }
    EXPECT_THROW(makeGpuOptimizer(layer, 5, 0.25, 1000, 35, -1),
                 std::invalid_argument);
    EXPECT_THROW(makeGpuOptimizer(layer, 5, 0.25, 1000, 35, 20),
                 std::invalid_argument);
  }
}
}  // namespace
}  // namespace delgpu

namespace delgpu {
namespace {
TEST(DelgpuOptimizer, CpuJumpOffsetChangesTargetAndRespectsDbuScale)
{
  initializeGpu();
  for (int scale : {1, 2}) {
    for (double jump_nm : {5.0, 35.0}) {
      auto layer = pair(ArcType::kB, 60 * scale);
      layer.rules = makeLayerRules(18 * scale);
      layer.arcs[0].ranges
          = {{.high2 = 0}, {.low2 = 180 * scale, .has_high = false}};
      layer.nodes[0].blocked = true;
      layer.nodes[0].low2 = layer.nodes[0].up2 = 0;
      layer.nodes[1].low2 = 60 * scale;
      layer.nodes[1].up2 = 166 * scale;
      auto gpu
          = makeGpuOptimizer(layer, 5 * scale, 0.25, 1000 * scale, jump_nm);
      gpu->step(10);
      gpu->step(10);
      const auto stats = gpu->step(10);
      const auto displacement = gpu->displacementDbu();
      EXPECT_EQ(stats.repaired, 1u);
      EXPECT_DOUBLE_EQ(displacement[0], 0);
      EXPECT_NEAR(30 * scale + displacement[1], (45 + jump_nm) * scale, 1e-8);
      EXPECT_NO_THROW(assessDisplacement(layer, displacement, 5 * scale));
      EXPECT_EQ(stats.remaining[1], 1u);
    }
  }
}
}  // namespace
}  // namespace delgpu
