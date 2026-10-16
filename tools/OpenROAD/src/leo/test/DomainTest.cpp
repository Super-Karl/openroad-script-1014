// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"
#include "leo/Graph.h"
#include "leo/Rules.h"
#include "leo/Trim.h"

namespace leo {
namespace {

// Width 10 DBU: the trim box is 10 long and 20 tall, the default prune cap is
// 200.
constexpr int kWidth = 10;
constexpr int64_t kCap2 = 2 * kDefaultPruneFactor * 5 * kWidth;

// One node on a horizontal layer, centered at (center_x2, center_y2).
Vnode nodeAt(int64_t center_x2, int64_t center_y2, bool high_end)
{
  Vnode node;
  node.center_x2 = center_x2;
  node.center_y2 = center_y2;
  node.high_end = high_end;
  return node;
}

LayerNodes layerWith(std::vector<Vnode> nodes)
{
  LayerNodes layer;
  layer.layer = "M2";
  layer.horizontal = true;
  layer.pitch = 2 * kWidth;
  layer.rules = makeLayerRules(kWidth);
  layer.nodes = std::move(nodes);
  return layer;
}

// Wire metal on the track of a trim: from `from` to `to` along x, one width
// tall around y = 0.
odb::Rect wire(int from, int to)
{
  return odb::Rect(from, -kWidth / 2, to, kWidth / 2);
}

TEST(LeoDomains, PaperPruneClipsAtIdealEnd)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, true), nodeAt(1000, 0, false)});
  layer.nodes[0].low2 = 0;
  layer.nodes[0].up2 = 1000;
  layer.nodes[1].low2 = 0;
  layer.nodes[1].up2 = 1000;
  EXPECT_EQ(pruneDomains(layer, 3), 2u);
  EXPECT_EQ(layer.nodes[0].low2, 0);
  EXPECT_EQ(layer.nodes[0].up2, 300);
  EXPECT_EQ(layer.nodes[1].low2, 700);
  EXPECT_EQ(layer.nodes[1].up2, 1000);
  EXPECT_EQ(layer.rules.move_limit, 150);
}

TEST(LeoDomains, PaperPrunePreservesShortDomainsAndBlockedNodes)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, -5), wire(35, 200)}, true);
  EXPECT_EQ(pruneDomains(layer, 3), 0u);
  EXPECT_EQ(layer.nodes[0].up2, 60);
  computeDomains(layer, {wire(-100, 0)}, true);
  EXPECT_EQ(pruneDomains(layer, 3), 0u);
  EXPECT_TRUE(layer.nodes[0].blocked);
  EXPECT_EQ(layer.nodes[0].up2, 0);
}

TEST(LeoDomains, LargerPruneFactorIsNotLimitedToOld20W)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, -5), wire(505, 600)}, true);
  EXPECT_EQ(layer.nodes[0].up2, 1000);
  pruneDomains(layer, 5);
  EXPECT_EQ(layer.nodes[0].up2, 500);  // 25W, greater than the old 20W cap
  EXPECT_THROW(pruneDomains(layer, 0), std::invalid_argument);
}

TEST(LeoDomains, PrunedDomainsRemoveOnlyAlwaysLegalArcs)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, true), nodeAt(450, 0, false)});
  for (size_t i = 0; i < layer.nodes.size(); ++i) {
    auto& n = layer.nodes[i];
    n.id = i;
    n.low2 = n.center_x2;
    n.up2 = n.center_x2 + 400;
  }
  // Same-sign ends permit a redundant A arc after clipping to P=50 DBU.
  layer.nodes[1].high_end = true;
  buildArcs(layer);
  ASSERT_EQ(layer.arcs.size(), 1u);
  pruneDomains(layer, 1);
  buildArcs(layer);
  EXPECT_TRUE(layer.arcs.empty());
}

TEST(LeoDomains, TravelStopsAtTheNearestMetal)
{
  // The own wire ends at the trim, the next wire starts 30 DBU above it.
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, -5), wire(35, 200)});

  const Vnode& node = layer.nodes[0];
  EXPECT_FALSE(node.blocked);
  EXPECT_EQ(node.l_move2, 0);   // the own metal blocks the low side
  EXPECT_EQ(node.u_move2, 60);  // 30 DBU of free space, doubled
  EXPECT_EQ(node.low2, 0);
  EXPECT_EQ(node.up2, 60);
}

TEST(LeoDomains, LowEndNodeMirrors)
{
  // The own wire starts at the trim, the next wire ends 30 DBU below it.
  LayerNodes layer = layerWith({nodeAt(0, 0, false)});
  computeDomains(layer, {wire(-200, -35), wire(5, 100)});

  const Vnode& node = layer.nodes[0];
  EXPECT_EQ(node.l_move2, 60);
  EXPECT_EQ(node.u_move2, 0);
  EXPECT_EQ(node.low2, -60);
  EXPECT_EQ(node.up2, 0);
}

TEST(LeoDomains, TravelIsCapped)
{
  // 50 W of free space above the trim, capped at 20 W.
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, -5), wire(505, 600)});
  EXPECT_EQ(layer.nodes[0].u_move2, kCap2);
  EXPECT_EQ(layer.nodes[0].l_move2, 0);

  // Without obstacles both directions reach the cap.
  LayerNodes empty = layerWith({nodeAt(0, 0, true)});
  computeDomains(empty, {});
  EXPECT_EQ(empty.nodes[0].l_move2, kCap2);
  EXPECT_EQ(empty.nodes[0].u_move2, kCap2);
}

TEST(LeoDomains, MetalOnOtherTracksDoesNotBlock)
{
  // The trim is 20 DBU tall; metal of the next tracks starts at y = 20 and
  // never limits the travel, even when it touches the band of the trim.
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, -5), odb::Rect(35, 20, 200, 30)});
  EXPECT_EQ(layer.nodes[0].u_move2, kCap2);

  LayerNodes further = layerWith({nodeAt(0, 0, true)});
  computeDomains(further, {odb::Rect(35, 45, 200, 55)});
  EXPECT_EQ(further.nodes[0].u_move2, kCap2);
}

TEST(LeoDomains, MetalOverlappingTheTrimBlocksIt)
{
  // A gap narrower than the trim: fixed metal is already inside the trim.
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  computeDomains(layer, {wire(-100, 0)});

  const Vnode& node = layer.nodes[0];
  EXPECT_TRUE(node.blocked);
  EXPECT_EQ(node.l_move2, 0);
  EXPECT_EQ(node.u_move2, 0);
  EXPECT_EQ(node.low2, 0);
  EXPECT_EQ(node.up2, 0);
}

TEST(LeoDomains, VerticalLayerUsesY)
{
  LayerNodes layer = layerWith({nodeAt(0, 0, true)});
  layer.horizontal = false;
  // Metal below the trim along y, and 30 DBU above it.
  computeDomains(layer,
                 {odb::Rect(-5, -100, 5, -5), odb::Rect(-5, 35, 5, 200)});

  const Vnode& node = layer.nodes[0];
  EXPECT_EQ(node.l_move2, 0);
  EXPECT_EQ(node.u_move2, 60);
  EXPECT_EQ(node.low2, 0);
  EXPECT_EQ(node.up2, 60);
}

TEST(LeoDomains, TrimsOfAGapFromMetal)
{
  // Two wire ends 20 DBU apart: both trims sit inside the gap, so each one can
  // travel by the gap minus its own length. The outer trims travel away from
  // their wire until the cap.
  const std::vector<odb::Rect> wires{wire(0, 40), wire(60, 100)};
  const std::vector<Trim> trims = makeTrimBoxes(wires, kWidth, true, "M2", 1);
  LayerNodes layer = layerWith(makeVnodes(trims, 0, trims.size(), true));
  computeDomains(layer, wires);

  ASSERT_EQ(layer.nodes.size(), 4u);
  EXPECT_EQ(layer.nodes[0].up2, layer.nodes[0].center_x2);
  EXPECT_EQ(layer.nodes[0].l_move2, kCap2);
  EXPECT_EQ(layer.nodes[3].low2, layer.nodes[3].center_x2);
  EXPECT_EQ(layer.nodes[3].u_move2, kCap2);

  // The two trims inside the gap face each other: gap - W = 10 DBU of travel.
  EXPECT_EQ(layer.nodes[1].l_move2, 0);
  EXPECT_EQ(layer.nodes[1].u_move2, 20);
  EXPECT_EQ(layer.nodes[2].l_move2, 20);
  EXPECT_EQ(layer.nodes[2].u_move2, 0);
}

}  // namespace
}  // namespace leo
