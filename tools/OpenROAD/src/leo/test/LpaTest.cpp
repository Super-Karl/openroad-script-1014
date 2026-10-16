// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "leo/Graph.h"
#include "leo/Lpa.h"
#include "leo/Rules.h"
#include "lpa/DifferenceConstraints.h"

namespace leo {
namespace {
// Width 10 gives D = Dt = 50 DBU, Du = 30 DBU and default P = 200 DBU; all
// positions below are doubled, as everywhere in the graph.
constexpr int kWidth = 10;
constexpr int64_t kPitch = 2 * kWidth;

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

// A facing pair on one track: the left trim is at the high end of its metal,
// the right one at the low end.
std::vector<Vnode> facingPair(int64_t gap2, int64_t travel2)
{
  return {nodeAt(0, 0, 0, 0, travel2, true),
          nodeAt(1, gap2, 0, travel2, 0, false)};
}

TEST(LeoLpaOracle, FeasibilityStartsAtDomainBounds)
{
  const auto result = solveDifferenceConstraints({0, 0}, {10, 0}, {{0, 1, 0}});
  ASSERT_TRUE(result.feasible);
  EXPECT_EQ(result.lower, (std::vector<int64_t>{0, 0}));
  EXPECT_EQ(result.upper, (std::vector<int64_t>{0, 0}));
}

TEST(LeoLpaOracle, UpperBoundsReachReverseOrderedPredecessors)
{
  const auto result = solveDifferenceConstraints(
      {0, 0, 0}, {100, 100, 0}, {{1, 0, 0}, {0, 2, 0}});
  ASSERT_TRUE(result.feasible);
  EXPECT_EQ(result.upper, (std::vector<int64_t>{0, 0, 0}));
}

TEST(LeoLpaOracle, DetectsPositiveCycleAndDoesNotRetainFailedTrial)
{
  EXPECT_FALSE(
      solveDifferenceConstraints({0, 0}, {100, 100}, {{0, 1, 1}, {1, 0, 0}})
          .feasible);
  const auto result
      = solveDifferenceConstraints({0, 0}, {100, 100}, {{0, 1, 1}});
  ASSERT_TRUE(result.feasible);
  EXPECT_EQ(result.lower, (std::vector<int64_t>{0, 1}));
}

TEST(LeoLpaOracle, MatchesExhaustiveFeasibleCoordinates)
{
  // Independent enumeration verifies feasibility and both componentwise
  // extrema, including cycles and negative distances (125 small models).
  for (int d0 = -2; d0 <= 2; ++d0) {
    for (int d1 = -2; d1 <= 2; ++d1) {
      for (int d2 = -2; d2 <= 2; ++d2) {
        const std::vector<Constraint> constraints{
            {0, 1, d0}, {1, 2, d1}, {2, 0, d2}};
        bool feasible = false;
        std::vector<int64_t> lower(3, 4), upper(3, -1);
        for (int x = 0; x <= 3; ++x) {
          for (int y = 0; y <= 3; ++y) {
            for (int z = 0; z <= 3; ++z) {
              if (y - x < d0 || z - y < d1 || x - z < d2) {
                continue;
              }
              feasible = true;
              const std::vector<int64_t> point{x, y, z};
              for (size_t i = 0; i < point.size(); ++i) {
                lower[i] = std::min(lower[i], point[i]);
                upper[i] = std::max(upper[i], point[i]);
              }
            }
          }
        }
        const auto actual
            = solveDifferenceConstraints({0, 0, 0}, {3, 3, 3}, constraints);
        ASSERT_EQ(actual.feasible, feasible) << d0 << "," << d1 << "," << d2;
        if (feasible) {
          EXPECT_EQ(actual.lower, lower);
          EXPECT_EQ(actual.upper, upper);
        }
      }
    }
  }
}

TEST(LeoLpa, IsolatedNodeKeepsItsPosition)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, 0, 40, 40, true)});
  buildArcs(layer);
  collectComponents(layer);

  const std::vector<SubgraphResult> results = solveLpa(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].arcs, 0u);
  EXPECT_EQ(results[0].moved, 0u);
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.lpa.subgraphs, 1u);
  EXPECT_EQ(layer.lpa.with_arcs, 0u);
}

TEST(LeoLpa, LegalPairUsesDirectMinimalFeasibleCoordinates)
{
  // Direct LPA returns the minimal feasible vector, even for an initially legal
  // pair. EMA is responsible for shortening the resulting extension.
  LayerNodes layer = layerWith(facingPair(200, 160));
  buildArcs(layer);
  collectComponents(layer);
  ASSERT_EQ(layer.arcs.size(), 1u);
  EXPECT_EQ(layer.arcs[0].type, ArcType::kB);

  const std::vector<SubgraphResult> results = solveLpa(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, SubgraphResult::Status::kSolved);
  EXPECT_EQ(results[0].moved, 1u);
  EXPECT_EQ(results[0].added_length2, 100);
  EXPECT_EQ(layer.arcs[0].selected_range, 1);  // the [Dt, +inf) range
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.nodes[1].pos_x2, 100);
}

TEST(LeoLpa, ViolatingPairMerges)
{
  // 60 apart with Dt = 100: in this model both trims only travel towards each
  // other. Direct minimal feasible coordinates merge them at the left node.
  LayerNodes layer = layerWith(facingPair(60, 100));
  buildArcs(layer);
  collectComponents(layer);
  ASSERT_EQ(layer.arcs.size(), 1u);

  const std::vector<SubgraphResult> results = solveLpa(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, SubgraphResult::Status::kSolved);
  EXPECT_EQ(layer.arcs[0].selected_range, 0);  // the merge range
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.nodes[1].pos_x2, 0);
  EXPECT_EQ(results[0].moved, 1u);
  EXPECT_EQ(results[0].added_length2, 60);
  EXPECT_FALSE(moved(layer.nodes[0]));
  EXPECT_TRUE(moved(layer.nodes[1]));
}

TEST(LeoLpa, ArcWithoutFeasibleRangeIsPartial)
{
  // Separating needs the right node to travel up, which its domain forbids;
  // merging needs the left node to travel 60, more than its 10.
  LayerNodes layer = layerWith(
      {nodeAt(0, 0, 0, 0, 10, true), nodeAt(1, 60, 0, 0, 0, false)});
  buildArcs(layer);
  collectComponents(layer);
  ASSERT_EQ(layer.arcs.size(), 1u);
  EXPECT_EQ(layer.arcs[0].type, ArcType::kB);

  const std::vector<SubgraphResult> results = solveLpa(layer);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].status, SubgraphResult::Status::kPartial);
  EXPECT_EQ(results[0].unresolved_arcs, 1u);
  EXPECT_EQ(layer.arcs[0].selected_range, -1);
  EXPECT_EQ(results[0].moved, 0u);
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.nodes[1].pos_x2, 60);
  EXPECT_EQ(layer.lpa.unresolved_arcs, 1u);
}

TEST(LeoLpa, AdjacentTrackPairAligns)
{
  // One track apart, 20 doubled DBU apart along the track: the merge range is
  // nearest to the ideal distance and feasible.
  LayerNodes layer = layerWith({nodeAt(0, 0, 0, 0, 100, true),
                                nodeAt(1, 20, 2 * kPitch, 100, 0, false)});
  buildArcs(layer);
  collectComponents(layer);
  ASSERT_EQ(layer.arcs.size(), 1u);
  EXPECT_EQ(layer.arcs[0].type, ArcType::kC);

  solveLpa(layer);
  EXPECT_EQ(layer.arcs[0].selected_range, 1);  // the merge range
  EXPECT_EQ(layer.nodes[0].pos_x2, 0);
  EXPECT_EQ(layer.nodes[1].pos_x2, 0);
}

TEST(LeoLpa, ComponentsSplitTheLayer)
{
  // Two independent facing pairs on tracks far apart.
  std::vector<Vnode> nodes = facingPair(60, 100);
  for (Vnode node : facingPair(60, 100)) {
    node.id += 2;
    node.center_y2 = 40 * kPitch;
    node.pos_y2 = 40 * kPitch;
    nodes.push_back(node);
  }
  LayerNodes layer = layerWith(std::move(nodes));
  buildArcs(layer);
  collectComponents(layer);
  ASSERT_EQ(layer.components.size(), 2u);
  ASSERT_EQ(layer.arcs.size(), 2u);

  const std::vector<SubgraphResult> results = solveLpa(layer);
  ASSERT_EQ(results.size(), 2u);
  EXPECT_EQ(layer.lpa.subgraphs, 2u);
  EXPECT_EQ(layer.lpa.with_arcs, 2u);
  EXPECT_EQ(layer.lpa.solved, 2u);
  EXPECT_EQ(layer.lpa.partial, 0u);
  EXPECT_EQ(layer.lpa.unresolved_arcs, 0u);
  EXPECT_EQ(layer.lpa.added_length2, 120);
  EXPECT_EQ(results[0].added_length2, 60);
  EXPECT_EQ(results[1].added_length2, 60);
}

}  // namespace
}  // namespace leo
