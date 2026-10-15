// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <vector>

#include "gtest/gtest.h"
#include "leo/Graph.h"
#include "leo/Rules.h"
#include "leo/Trim.h"

namespace leo {
namespace {

std::vector<Trim> trimsOf(const std::vector<odb::Rect>& metal,
                          bool horizontal,
                          int width = 10)
{
  return makeTrimBoxes(metal, width, horizontal, horizontal ? "M2" : "M3", 1);
}

TEST(LeoVnodes, RulesScaleLayerWidth)
{
  const LayerRules rules = makeLayerRules(32);
  EXPECT_EQ(rules.width, 32);
  EXPECT_EQ(rules.d, 5 * 32);
  EXPECT_EQ(rules.dt, 5 * 32);
  EXPECT_EQ(rules.du, 3 * 32);
}

TEST(LeoVnodes, OneTrimPerVnode)
{
  const std::vector<Trim> trims = trimsOf({{0, 0, 100, 10}}, true);
  const std::vector<Vnode> nodes = makeVnodes(trims, 0, trims.size(), true);
  ASSERT_EQ(nodes.size(), 2u);

  // Trims are {-10, -5, 0, 15} and {100, -5, 110, 15}; the center is the trim
  // box center, kept doubled.
  EXPECT_EQ(nodes[0].id, 0);
  EXPECT_EQ(nodes[0].trim_index, 0u);
  EXPECT_EQ(nodes[0].net_id, 1u);
  EXPECT_EQ(nodes[0].trim, odb::Rect(-10, -5, 0, 15));
  EXPECT_EQ(nodes[0].center_x2, -10);
  EXPECT_EQ(nodes[0].center_y2, 10);
  EXPECT_EQ(center(nodes[0]), odb::Point(-5, 5));
  // The metal end the trim belongs to is kept on the node.
  EXPECT_FALSE(nodes[0].high_end);
  EXPECT_TRUE(nodes[1].high_end);

  EXPECT_EQ(nodes[1].id, 1);
  EXPECT_EQ(nodes[1].trim_index, 1u);
  EXPECT_EQ(nodes[1].center_x2, 210);
  EXPECT_EQ(nodes[1].center_y2, 10);
}

TEST(LeoVnodes, IdsFollowPreferredDirection)
{
  const std::vector<Trim> trims = trimsOf({{0, 0, 100, 10}}, true);
  // Reversed input must produce the same ids: they come from the geometry.
  const std::vector<Trim> reversed{trims[1], trims[0]};
  const std::vector<Vnode> nodes
      = makeVnodes(reversed, 0, reversed.size(), true);
  ASSERT_EQ(nodes.size(), 2u);
  EXPECT_EQ(nodes[0].trim_index, 1u);
  EXPECT_EQ(nodes[1].trim_index, 0u);
  EXPECT_LT(nodes[0].center_x2, nodes[1].center_x2);

  // On a vertical layer the id order follows y instead of x.
  const std::vector<Trim> vertical = trimsOf({{0, 0, 10, 100}}, false);
  const std::vector<Trim> vertical_reversed{vertical[1], vertical[0]};
  const std::vector<Vnode> vertical_nodes
      = makeVnodes(vertical_reversed, 0, vertical_reversed.size(), false);
  ASSERT_EQ(vertical_nodes.size(), 2u);
  EXPECT_EQ(vertical_nodes[0].trim_index, 1u);
  EXPECT_LT(vertical_nodes[0].center_y2, vertical_nodes[1].center_y2);
}

TEST(LeoVnodes, RangeSelectsOneLayer)
{
  std::vector<Trim> trims = trimsOf({{0, 0, 100, 10}}, true);
  const std::vector<Trim> other = trimsOf({{0, 0, 10, 100}}, false);
  trims.insert(trims.end(), other.begin(), other.end());

  const std::vector<Vnode> nodes = makeVnodes(trims, 2, trims.size(), false);
  ASSERT_EQ(nodes.size(), 2u);
  // The absolute trim index is kept, so a node still points at its trim.
  EXPECT_EQ(nodes[0].trim_index, 2u);
  EXPECT_EQ(nodes[1].trim_index, 3u);

  EXPECT_TRUE(makeVnodes(trims, 2, 2, false).empty());
}

TEST(LeoVnodes, CenterDistanceUsesTrimCenters)
{
  const std::vector<Trim> trims = trimsOf({{0, 0, 100, 10}}, true);
  const std::vector<Vnode> nodes = makeVnodes(trims, 0, trims.size(), true);
  // Centers are (-5, 5) and (105, 5): 110 DBU apart along the preferred axis.
  EXPECT_EQ(centerDistance2(nodes[0], nodes[1]), 220);
  EXPECT_DOUBLE_EQ(centerDistance(nodes[0], nodes[1]), 110.0);

  // An odd trim size puts the center on a half DBU; doubling keeps it exact.
  const std::vector<Trim> odd{{.box = odb::Rect(0, 0, 3, 7),
                               .layer = "M2",
                               .net_id = 1,
                               .high_end = false},
                              {.box = odb::Rect(7, 0, 10, 7),
                               .layer = "M2",
                               .net_id = 2,
                               .high_end = true}};
  const std::vector<Vnode> odd_nodes = makeVnodes(odd, 0, odd.size(), true);
  ASSERT_EQ(odd_nodes.size(), 2u);
  EXPECT_EQ(odd_nodes[0].center_x2, 3);
  EXPECT_EQ(odd_nodes[0].center_y2, 7);
  EXPECT_EQ(centerDistance2(odd_nodes[0], odd_nodes[1]), 14);
  EXPECT_DOUBLE_EQ(centerDistance(odd_nodes[0], odd_nodes[1]), 7.0);
}

TEST(LeoVnodes, CenterOffsetsUsePreferredDirection)
{
  const std::vector<Trim> horizontal = trimsOf({{0, 0, 100, 10}}, true);
  const std::vector<Vnode> hnodes
      = makeVnodes(horizontal, 0, horizontal.size(), true);
  ASSERT_EQ(hnodes.size(), 2u);
  const CenterOffset along = centerOffset(hnodes[0], hnodes[1], true);
  EXPECT_EQ(along.ds2, 220);
  EXPECT_EQ(along.dt2, 0);

  // Same boxes read as a vertical layer: the preferred offset is along y.
  const CenterOffset swapped = centerOffset(hnodes[0], hnodes[1], false);
  EXPECT_EQ(swapped.ds2, 0);
  EXPECT_EQ(swapped.dt2, 220);

  const std::vector<Trim> vertical = trimsOf({{0, 0, 10, 100}}, false);
  const std::vector<Vnode> vnodes
      = makeVnodes(vertical, 0, vertical.size(), false);
  ASSERT_EQ(vnodes.size(), 2u);
  const CenterOffset down = centerOffset(vnodes[0], vnodes[1], false);
  EXPECT_EQ(down.ds2, 220);
  EXPECT_EQ(down.dt2, 0);
}

}  // namespace
}  // namespace leo
