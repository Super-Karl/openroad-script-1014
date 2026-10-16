// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "leo/Ema.h"
#include "leo/Graph.h"
#include "leo/Lpa.h"
#include "leo/Rules.h"

namespace leo {
namespace {
// Width 10 gives D = Dt = 50 DBU, Du = 30 DBU and default P = 200 DBU; all
// positions are doubled, as everywhere in the graph.
constexpr int kWidth = 10;
constexpr int64_t kPitch = 2 * kWidth;
constexpr int64_t kD2 = 2 * 50;  // D in doubled DBU

Vnode nodeAt(int id,
             int64_t center_x2,
             int64_t center_y2,
             int64_t l_move2,
             int64_t u_move2,
             bool high_end)
{
  Vnode node;
  node.id = id;
  node.center_x2 = center_x2;
  node.center_y2 = center_y2;
  node.high_end = high_end;
  node.l_move2 = l_move2;
  node.u_move2 = u_move2;
  node.low2 = center_x2 - l_move2;
  node.up2 = center_x2 + u_move2;
  node.pos_x2 = center_x2;
  node.pos_y2 = center_y2;
  return node;
}

LayerNodes layerWith(std::vector<Vnode> nodes)
{
  LayerNodes layer;
  layer.layer = "M2";
  layer.horizontal = true;
  layer.pitch = kPitch;
  layer.dbu = 1000;
  layer.rules = makeLayerRules(kWidth);
  layer.nodes = std::move(nodes);
  return layer;
}

// One arc of the given type whose only range is [low, +inf).
Arc arcWithLowerBound(int id, int src, int dst, ArcType type, int64_t low2)
{
  Arc arc;
  arc.id = id;
  arc.src = src;
  arc.dst = dst;
  arc.type = type;
  arc.ranges = {{.low2 = low2, .has_high = false}};
  arc.selected_range = 0;
  return arc;
}

TEST(LeoEma, FixedFirstImprovementDoesNotSelectGlobalBestGain)
{
  LayerNodes layer = layerWith({nodeAt(0, 10, 0, 10, 0, false),
                                nodeAt(1, 0, 0, 0, 10, true),
                                nodeAt(2, 10, 0, 10, 0, false),
                                nodeAt(3, 10, 0, 10, 0, false)});
  layer.arcs = {arcWithLowerBound(0, 0, 1, ArcType::kA, -3),
                arcWithLowerBound(1, 1, 2, ArcType::kA, -2),
                arcWithLowerBound(2, 2, 3, ArcType::kA, 2),
                arcWithLowerBound(3, 0, 3, ArcType::kA, -1)};
  collectComponents(layer);
  layer.nodes[0].pos_x2 = 0;
  layer.nodes[1].pos_x2 = 0;
  layer.nodes[2].pos_x2 = 0;
  layer.nodes[3].pos_x2 = 2;
  const auto result = solveEma(layer);
  ASSERT_EQ(result.size(), 1u);
  EXPECT_EQ(result[0].moves, 5u);
  EXPECT_EQ(layer.nodes[0].pos_x2, 10);
  EXPECT_EQ(layer.nodes[1].pos_x2, 7);
  EXPECT_EQ(layer.nodes[2].pos_x2, 8);
  EXPECT_EQ(layer.nodes[3].pos_x2, 10);
  EXPECT_EQ(result[0].length_after2, 9);
  // The removed global-best/dual-direction policy instead ends at [3,0,8,10].
}

TEST(LeoEma, NoAcceptedArcStillAccountsForAndMinimizesLpaExtension)
{
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 0, 10, true), nodeAt(1, 60, 0, 40, 0, false)});
  buildArcs(layer);
  collectComponents(layer);
  solveLpa(layer);
  ASSERT_EQ(layer.arcs[0].selected_range, -1);
  EXPECT_EQ(layer.nodes[1].pos_x2, 20);
  solveEma(layer);
  EXPECT_EQ(layer.nodes[1].pos_x2, 60);
  EXPECT_EQ(layer.ema.length_before2, 40);
  EXPECT_EQ(layer.ema.length_after2, 0);
}

TEST(LeoEma, NodeWithoutArcStaysUnchanged)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, 0, 40, 40, true)});
  buildArcs(layer);
  collectComponents(layer);

  const std::vector<EmaResult> results = solveEma(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].arcs, 0u);
  EXPECT_EQ(results[0].status, EmaResult::Status::kUnchanged);
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
}

TEST(LeoEma, MergedPairCannotImprove)
{
  // A merged pair trades metal between the two ends: the total is fixed, so no
  // move shortens the extent.
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 0, 100, true), nodeAt(1, 60, 0, 100, 0, false)});
  buildArcs(layer);
  collectComponents(layer);
  solveLpa(layer);
  ASSERT_EQ(layer.nodes[0].pos_x2, 0);  // direct minimal feasible solution

  const std::vector<EmaResult> results = solveEma(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, EmaResult::Status::kUnchanged);
  EXPECT_EQ(results[0].moves, 0u);
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
}

TEST(LeoEma, TightTreeUnExtendsTogether)
{
  // Two low end trims on one track, joined by an arc that needs D between them.
  // LPA satisfied it by pulling the left trim down; EMA can move the whole
  // tight closure back up, which removes both extensions at once.
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 200, 0, false), nodeAt(1, 100, 0, 200, 0, false)});
  layer.arcs.push_back(arcWithLowerBound(0, 0, 1, ArcType::kA, kD2));
  collectComponents(layer);
  // LPA left the left trim extended by 100 to keep the D separation.
  layer.nodes[0].pos_x2 = -100;
  layer.nodes[1].pos_x2 = 0;

  const std::vector<EmaResult> results = solveEma(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, EmaResult::Status::kImproved);
  EXPECT_EQ(results[0].moves, 1u);
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.nodes[1].pos_x2, 100);
  // Both extensions are gone.
  EXPECT_EQ(results[0].length_before2, 200);
  EXPECT_EQ(results[0].length_after2, 0);
}

TEST(LeoEma, DomainStopsTheMove)
{
  // Same chain, but the right node cannot travel up far enough to let the pair
  // reach the ideal positions: the step is cut by its domain.
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 200, 0, false), nodeAt(1, 40, 0, 200, 0, false)});
  layer.arcs.push_back(arcWithLowerBound(0, 0, 1, ArcType::kA, kD2));
  collectComponents(layer);
  layer.nodes[0].pos_x2 = -100;
  layer.nodes[1].pos_x2 = 0;

  const std::vector<EmaResult> results = solveEma(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, EmaResult::Status::kImproved);
  // Both trims travel by 40, the room left above the right one.
  EXPECT_EQ(layer.nodes[0].pos_x2, -60);
  EXPECT_EQ(layer.nodes[1].pos_x2, 40);
  EXPECT_EQ(results[0].length_before2, 140);
  EXPECT_EQ(results[0].length_after2, 60);
}

TEST(LeoEma, CrossingArcLimitsTheStep)
{
  // The closure cannot travel past the slack of the arc leaving it.
  LayerNodes layer = layerWith({nodeAt(0, 0, 0, 200, 0, false),
                                nodeAt(1, 100, 0, 200, 0, false),
                                nodeAt(2, 300, 0, 0, 0, true)});
  layer.arcs.push_back(arcWithLowerBound(0, 0, 1, ArcType::kA, kD2));
  // The right trim must stay 250 above the middle one, 50 of slack.
  layer.arcs.push_back(arcWithLowerBound(1, 1, 2, ArcType::kA, 250));
  collectComponents(layer);
  layer.nodes[0].pos_x2 = -100;
  layer.nodes[1].pos_x2 = 0;
  layer.nodes[2].pos_x2 = 300;

  const std::vector<EmaResult> results = solveEma(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, EmaResult::Status::kImproved);
  // The closure {0, 1} may travel up by the slack of the crossing arc only.
  EXPECT_EQ(layer.nodes[0].pos_x2, -50);
  EXPECT_EQ(layer.nodes[1].pos_x2, 50);
  EXPECT_EQ(layer.nodes[2].pos_x2, 300);
}

TEST(LeoEma, SubgraphStatsAreCollected)
{
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 200, 0, false), nodeAt(1, 100, 0, 200, 0, false)});
  layer.arcs.push_back(arcWithLowerBound(0, 0, 1, ArcType::kA, kD2));
  collectComponents(layer);
  layer.nodes[0].pos_x2 = -100;
  layer.nodes[1].pos_x2 = 0;

  solveEma(layer);
  EXPECT_EQ(layer.ema.subgraphs, 1u);
  EXPECT_EQ(layer.ema.improved, 1u);
  EXPECT_EQ(layer.ema.unchanged, 0u);
  EXPECT_EQ(layer.ema.limited, 0u);
  EXPECT_EQ(layer.ema.moves, 1u);
  EXPECT_EQ(layer.ema.length_before2, 200);
  EXPECT_EQ(layer.ema.length_after2, 0);
}

}  // namespace
}  // namespace leo
