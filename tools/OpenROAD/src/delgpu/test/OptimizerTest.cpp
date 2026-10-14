// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <cmath>

#include "delgpu/Optimizer.h"
#include "gtest/gtest.h"

namespace delgpu {
namespace {
LayerNodes pair(ArcType type, int64_t distance2)
{
  LayerNodes l;
  l.rules = makeLayerRules(10);
  Vnode a;
  a.id = 0;
  a.low2 = -100;
  a.up2 = 100;
  Vnode b = a;
  b.id = 1;
  b.center_x2 = distance2;
  b.low2 += distance2;
  b.up2 += distance2;
  l.nodes = {a, b};
  Arc arc;
  arc.src = 0;
  arc.dst = 1;
  arc.type = type;
  if (type == ArcType::kA) {
    arc.ranges = {{.low2 = 100, .has_high = false}};
  } else if (type == ArcType::kB) {
    arc.ranges = {{.high2 = 0}, {.low2 = 100, .has_high = false}};
  } else {
    arc.ranges = {{.has_low = false, .high2 = -60},
                  {.high2 = 0},
                  {.low2 = 60, .has_high = false}};
  }
  l.arcs = {arc};
  return l;
}

// Block downhill motion while leaving a route across the peak available.
LayerNodes stalledPair(ArcType type, int64_t distance2, bool increase_distance)
{
  auto layer = pair(type, distance2);
  if (increase_distance) {
    layer.nodes[0].up2 = 0;
    layer.nodes[1].low2 = distance2;
  } else {
    layer.nodes[0].low2 = 0;
    layer.nodes[1].up2 = distance2;
  }
  return layer;
}

// Add legal, stationary neighboring arcs so the tested pair has unequal
// endpoint degrees without adding another gradient or a competing jump.
void addLegalNeighbors(LayerNodes& layer, int endpoint, int count)
{
  for (int i = 0; i < count; ++i) {
    Vnode node;
    node.id = layer.nodes.size();
    node.center_x2 = 200 + 20 * i;
    node.low2 = node.up2 = node.center_x2;
    node.blocked = true;
    layer.nodes.push_back(node);
    Arc arc;
    arc.id = layer.arcs.size();
    arc.src = endpoint;
    arc.dst = node.id;
    arc.type = ArcType::kA;
    arc.ranges = {{.low2 = 100, .has_high = false}};
    layer.arcs.push_back(arc);
  }
}

TEST(DelOptimizer, StalledArcJumpsOnThirdRoundAcrossEitherSide)
{
  for (const auto type : {ArcType::kB, ArcType::kC}) {
    for (const bool increase : {false, true}) {
      for (const int sign : {-1, 1}) {
        if (type == ArcType::kB && sign < 0) {
          continue;
        }
        const int midpoint = (type == ArcType::kB ? 25 : 15) * sign;
        const int start = midpoint + (increase ? -5 : 5);
        auto layer = stalledPair(type, 2 * start, increase);
        LayerOptimizer optimizer(layer, 5.0);
        EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
        EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
        const auto stats = optimizer.step(10);
        EXPECT_EQ(stats.stall_triggered, 1u);
        EXPECT_EQ(stats.stall_repaired, 1u);
        const auto delta = optimizer.displacementDbu();
        EXPECT_NEAR(
            start + delta[1] - delta[0], midpoint + (increase ? 1 : -1), 1e-10);
        EXPECT_NEAR(delta[0], increase ? -3.0 : 3.0, 1e-10);
        EXPECT_NEAR(delta[1], increase ? 3.0 : -3.0, 1e-10);
        EXPECT_NEAR(stats.max_repair_dbu, 3.0, 1e-10);
        EXPECT_EQ(stats.violations[static_cast<size_t>(type)], 1u);
        EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
      }
    }
  }
}
TEST(DelOptimizer, BarrierJumpWeightsBothEndpointsByIncidentArcCount)
{
  for (const int connected_endpoint : {0, 1}) {
    auto layer = stalledPair(ArcType::kB, 40, true);
    addLegalNeighbors(layer, connected_endpoint, 4);
    // Target pair needs +6 DBU; resistances are 6:2 or 2:6.
    LayerOptimizer optimizer(layer, 5.0);
    EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
    EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
    const auto stats = optimizer.step(10);
    EXPECT_EQ(stats.stall_triggered, 1u);
    EXPECT_EQ(stats.stall_repaired, 1u);
    const auto delta = optimizer.displacementDbu();
    const double src_share = connected_endpoint == 0 ? 1.5 : 4.5;
    EXPECT_NEAR(delta[0], -src_share, 1e-10);
    EXPECT_NEAR(delta[1], 6.0 - src_share, 1e-10);
    EXPECT_NEAR(20 + delta[1] - delta[0], 26.0, 1e-10);
    EXPECT_NEAR(stats.max_repair_dbu, 4.5, 1e-10);
    for (size_t i = 2; i < delta.size(); ++i) {
      EXPECT_EQ(delta[i], 0);
    }
  }
}

TEST(DelOptimizer, WeightedBarrierJumpRedistributesAtDomainLimit)
{
  auto layer = stalledPair(ArcType::kB, 40, true);
  addLegalNeighbors(layer, 0, 4);  // Preferred shares are src=1.5, dst=4.5.
  layer.nodes[0].low2 = -2;        // src can only move 1 DBU.
  LayerOptimizer optimizer(layer, 5.0);
  optimizer.step(10);
  optimizer.step(10);
  const auto stats = optimizer.step(10);
  EXPECT_EQ(stats.stall_repaired, 1u);
  const auto delta = optimizer.displacementDbu();
  EXPECT_NEAR(delta[0], -1.0, 1e-10);
  EXPECT_NEAR(delta[1], 5.0, 1e-10);
  EXPECT_NEAR(20 + delta[1] - delta[0], 26.0, 1e-10);
}

TEST(DelOptimizer, BarrierJumpUsesPhysicalNmAndFallsBackToOtherEndpoint)
{
  auto layer = stalledPair(ArcType::kB, 40, true);
  layer.nodes[0].blocked = true;
  LayerOptimizer optimizer(layer, 10.0, 2000);
  optimizer.step(10);
  optimizer.step(10);
  const auto stats = optimizer.step(10);
  EXPECT_EQ(stats.stall_repaired, 1u);
  const auto delta = optimizer.displacementDbu();
  EXPECT_EQ(delta[0], 0);
  EXPECT_NEAR(20 + delta[1], 27, 1e-10);  // 25 DBU + 1 nm (2 DBU).
}
TEST(DelOptimizer, JointJumpSucceedsWhenNeitherEndpointCanJumpAlone)
{
  auto layer = stalledPair(ArcType::kB, 40, true);
  layer.nodes[0].low2 = -8;  // Each endpoint can move 4 DBU; need 6 in total.
  layer.nodes[1].up2 = 48;
  LayerOptimizer optimizer(layer, 5.0);
  optimizer.step(10);
  optimizer.step(10);
  EXPECT_EQ(optimizer.step(10).stall_repaired, 1u);
  const auto delta = optimizer.displacementDbu();
  EXPECT_NEAR(delta[0], -3.0, 1e-10);
  EXPECT_NEAR(delta[1], 3.0, 1e-10);
}
TEST(DelOptimizer, JointJumpRedistributesAtEitherEndpointLimit)
{
  for (const bool limit_src : {false, true}) {
    auto layer = stalledPair(ArcType::kB, 40, true);
    layer.nodes[0].low2 = limit_src ? -4 : -8;
    layer.nodes[1].up2 = limit_src ? 48 : 44;
    LayerOptimizer optimizer(layer, 5.0);
    optimizer.step(10);
    optimizer.step(10);
    const auto stats = optimizer.step(10);
    EXPECT_EQ(stats.stall_repaired, 1u);
    const auto delta = optimizer.displacementDbu();
    EXPECT_NEAR(delta[0], limit_src ? -2.0 : -4.0, 1e-10);
    EXPECT_NEAR(delta[1], limit_src ? 4.0 : 2.0, 1e-10);
    EXPECT_NEAR(stats.max_repair_dbu, 4.0, 1e-10);
    EXPECT_NEAR(20 + delta[1] - delta[0], 26.0, 1e-10);
  }
}

TEST(DelOptimizer, BarrierJumpDoesNotLeaveDomainOrTreatAAsBump)
{
  auto layer = stalledPair(ArcType::kB, 40, true);
  layer.nodes[0].low2 = -2;
  layer.nodes[1].up2 = 42;
  LayerOptimizer optimizer(layer, 5.0);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(optimizer.step(10).stall_repaired, 0u);
  }
  EXPECT_EQ(optimizer.positions(), (std::vector<double>{0, 0}));
  auto a = stalledPair(ArcType::kA, 40, false);
  LayerOptimizer one_sided(a, 5.0);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(one_sided.step(10).stall_repaired, 0u);
  }
}
TEST(DelOptimizer, SharedEndpointJumpResetsOtherArcs)
{
  auto layer = stalledPair(ArcType::kB, 40, true);
  layer.arcs.push_back(layer.arcs.front());
  LayerOptimizer optimizer(layer, 5.0);
  optimizer.step(10);
  optimizer.step(10);
  const auto stats = optimizer.step(10);
  EXPECT_EQ(stats.stall_triggered, 1u);
  EXPECT_EQ(stats.stall_repaired, 1u);
}
TEST(DelOptimizer, MovingOrBelowToleranceArcDoesNotTrigger)
{
  auto below = stalledPair(ArcType::kB, 8, true);
  LayerOptimizer tolerated(below, 5.0);
  auto moving = pair(ArcType::kB, 40);
  LayerOptimizer active(moving, 5.0);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(tolerated.step(10).stall_triggered, 0u);
    EXPECT_EQ(active.step(10).stall_triggered, 0u);
  }
}

TEST(DelOptimizer, SubDbuMotionStillTriggersAfterThreeVisuallyStationaryRounds)
{
  for (const int distance2 : {40, 60}) {
    auto layer = pair(ArcType::kB, distance2);
    LayerOptimizer optimizer(layer, 5.0);
    for (int i = 0; i < 2; ++i) {
      const auto stats = optimizer.step(0.01);
      EXPECT_EQ(stats.stall_triggered, 0u);
      for (const double delta : optimizer.displacementDbu()) {
        EXPECT_NE(delta, 0.0);  // The continuous optimizer did move.
        EXPECT_EQ(std::llround(delta), 0);
      }
    }
    const auto stats = optimizer.step(0.01);
    EXPECT_EQ(stats.stall_triggered, 1u);
    EXPECT_EQ(stats.stall_repaired, 1u);
    const auto delta = optimizer.displacementDbu();
    EXPECT_NEAR(distance2 * 0.5 + delta[1] - delta[0],
                distance2 == 40 ? 26.0 : 24.0,
                1e-10);
  }
}
TEST(DelOptimizer, AccumulatedSubDbuMotionCrossingGridResetsCounter)
{
  auto layer = pair(ArcType::kB, 40);
  LayerOptimizer optimizer(layer, 5.0);
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(optimizer.step(0.15).stall_triggered, 0u);
    for (const double delta : optimizer.displacementDbu()) {
      EXPECT_EQ(std::llround(delta), 0);
    }
  }
  const auto previous = optimizer.displacementDbu();
  const auto stats = optimizer.step(0.15);
  const auto current = optimizer.displacementDbu();
  for (size_t i = 0; i < current.size(); ++i) {
    EXPECT_LT(std::abs(current[i] - previous[i]), 1.0);
    EXPECT_NE(std::llround(current[i]), std::llround(previous[i]));
  }
  EXPECT_EQ(stats.stall_triggered, 0u);
}

TEST(DelOptimizer, AnalyticRuleGradients)
{
  for (auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
    for (double d : {-7.0, -3.2, -0.7, 0.0, 0.7, 2.5, 4.2, 5.0, 7.0}) {
      constexpr double h = 1e-6;
      const auto p = rulePotential(type, d, 5);
      const double numeric = (rulePotential(type, d + h, 5).value
                              - rulePotential(type, d - h, 5).value)
                             / (2 * h);
      EXPECT_NEAR(p.derivative, numeric, 2e-6);
      EXPECT_GE(p.value, 0);
    }
  }
}
TEST(DelOptimizer, RepairToleranceExcludesStrictlyLessThanFiveDbu)
{
  // At 1000 DBU/um, 5 DBU is 5 nm. Include the exact boundary and zero.
  for (const auto type : {ArcType::kA, ArcType::kB, ArcType::kC}) {
    for (const int gap : {0, 4, 5, 6}) {
      const int threshold2 = type == ArcType::kC ? 60 : 100;
      auto layer = pair(type, threshold2 - 2 * gap);
      const LayerOptimizer optimizer(layer, 5.0);
      const auto stats = optimizer.evaluate(10);
      const auto index = static_cast<size_t>(type);
      EXPECT_EQ(stats.violations[index], gap > 0 ? 1u : 0u);
      EXPECT_EQ(stats.remaining_violations[index], gap >= 5 ? 1u : 0u);
    }
  }
}
TEST(DelOptimizer, RepairToleranceDoesNotChangeGradientSteps)
{
  auto layer = pair(ArcType::kB, 92);  // 4 DBU from the legal separation.
  LayerOptimizer exact(layer), tolerant(layer, 5.0);
  EXPECT_EQ(exact.evaluate(10).remaining_violations[1], 1u);
  EXPECT_EQ(tolerant.evaluate(10).remaining_violations[1], 0u);
  // The threshold gates barrier jumps after three visually stationary rounds.
  // Before that, it must not change the continuous gradient or its objective.
  for (int i = 0; i < 2; ++i) {
    const auto a = exact.step(10);
    const auto b = tolerant.step(10);
    EXPECT_EQ(exact.positions(), tolerant.positions());
    EXPECT_EQ(a.energy, b.energy);
    EXPECT_EQ(a.violations, b.violations);
  }
}
TEST(DelOptimizer, BNegativeDistanceIsPenalized)
{
  EXPECT_GT(rulePotential(ArcType::kB, -1, 5).value, 0);
  EXPECT_LT(rulePotential(ArcType::kB, -1, 5).derivative, 0);
  EXPECT_EQ(rulePotential(ArcType::kB, 0, 5).value, 0);
  EXPECT_EQ(rulePotential(ArcType::kB, 5, 5).value, 0);
}
TEST(DelOptimizer, DescentStaysInDomainAndPreservesInput)
{
  auto l = pair(ArcType::kA, 20);
  LayerOptimizer optimizer(l);
  double previous = optimizer.evaluate(10).energy;
  for (int i = 0; i < 100; ++i) {
    const auto s = optimizer.step(10);
    EXPECT_LE(s.energy, previous);
    previous = s.energy;
    for (double x : optimizer.positions()) {
      EXPECT_LE(std::abs(x), 5);
    }
  }
  EXPECT_LT(optimizer.evaluate(10).rule_energy, 1e-4);
  EXPECT_EQ(l.nodes[0].center_x2, 0);
  EXPECT_EQ(l.nodes[1].center_x2, 20);
}
TEST(DelOptimizer, EscapesMidpointDeterministically)
{
  auto l = pair(ArcType::kB, 50);
  LayerOptimizer a(l), b(l);
  const double before = a.evaluate(10).rule_energy;
  for (int i = 0; i < 100; ++i) {
    a.step(10);
    b.step(10);
  }
  EXPECT_LT(a.evaluate(10).rule_energy, before * 0.01);
  EXPECT_EQ(a.positions(), b.positions());
}
TEST(DelOptimizer, BlockedNodesRemainFixedAndViolationsRemainVisible)
{
  auto l = pair(ArcType::kA, 20);
  for (auto& n : l.nodes) {
    n.blocked = true;
  }
  LayerOptimizer optimizer(l);
  const auto s = optimizer.step(10);
  EXPECT_EQ(optimizer.positions(), (std::vector<double>{0, 0}));
  EXPECT_EQ(s.violations[0], 1u);
  EXPECT_FALSE(s.accepted);
}
TEST(DelOptimizer, VerticalAndHorizontalGiveSameDisplacement)
{
  auto h = pair(ArcType::kC, 20);
  auto v = h;
  v.horizontal = false;
  for (auto& n : v.nodes) {
    n.center_y2 = n.center_x2;
    n.center_x2 = 0;
  }
  LayerOptimizer a(h), b(v);
  for (int i = 0; i < 20; ++i) {
    a.step(10);
    b.step(10);
  }
  EXPECT_EQ(a.displacementDbu(), b.displacementDbu());
}
}  // namespace
}  // namespace delgpu
