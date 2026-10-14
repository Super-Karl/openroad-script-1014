// SPDX-License-Identifier: BSD-3-Clause
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
TEST(DelgpuOptimizer, MidpointAndNegativeDistanceBecomeLegal)
{
  initializeGpu();
  for (auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
    for (int sign : {-1, 1}) {
      auto layer = pair(type, sign * (type == ArcType::kC ? 30 : 50));
      auto gpu = makeGpuOptimizer(layer, 5, 0.2);
      for (int i = 0; i < 100 && count(gpu->evaluate(80)) > 0; ++i) {
        gpu->step(80);
      }
      auto stats = gpu->restoreBest(80);
      const auto assessed
          = assessDisplacement(layer, gpu->displacementDbu(), 5);
      EXPECT_EQ(count(stats), 0u);
      EXPECT_EQ(count(assessed), 0u);
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
  auto stats = gpu->restoreBest(10);
  auto x = gpu->displacementDbu();
  EXPECT_EQ(count(stats), 1u);
  EXPECT_DOUBLE_EQ(x[0], 0);
  EXPECT_DOUBLE_EQ(x[1], 0);
  EXPECT_NO_THROW(assessDisplacement(layer, x, 5));
}
TEST(DelgpuOptimizer, SharedEndpointsAndBestSnapshot)
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
  const auto best = gpu->restoreBest(80);
  const auto checked = assessDisplacement(layer, gpu->displacementDbu(), 5);
  EXPECT_LE(count(best), count(initial));
  EXPECT_EQ(count(best), count(checked));
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
TEST(DelgpuOptimizer, ArbitraryLegalIntervalsAreUsedByRepairAndAssessment)
{
  initializeGpu();
  auto layer = pair(ArcType::kB, 50);
  layer.arcs[0].ranges = {{.low2 = 70, .high2 = 80}};
  auto gpu = makeGpuOptimizer(layer, 5, 0.2);
  for (int i = 0; i < 60 && count(gpu->evaluate(80)) > 0; ++i) {
    gpu->step(80);
  }
  const auto best = gpu->restoreBest(80);
  EXPECT_EQ(count(best), 0u);
  EXPECT_EQ(count(assessDisplacement(layer, gpu->displacementDbu(), 5)), 0u);
}
}  // namespace
}  // namespace delgpu

namespace delgpu {
namespace {
TEST(DelgpuOptimizer, JumpOffsetChangesFallbackTargetAndRespectsDbuScale)
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
      gpu->restoreBest(10);
      EXPECT_LE(gpu->evaluate(10).max_violation_dbu, 30 * scale + 1e-8);
    }
  }
}
}  // namespace
}  // namespace delgpu
