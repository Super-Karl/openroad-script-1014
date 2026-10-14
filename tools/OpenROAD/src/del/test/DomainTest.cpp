// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <cstdint>
#include <vector>

#include "del/Graph.h"
#include "del/Rules.h"
#include "del/Trim.h"
#include "gtest/gtest.h"

namespace del {
namespace {

// Width 10 DBU: the trim box is 10 long and 20 tall, the travel cap is 200.
constexpr int kWidth = 10;
constexpr int64_t kCap2 = 2 * 20 * kWidth;

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

TEST(DelDomains, TravelStopsAtTheNearestMetal)
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

TEST(DelDomains, LowEndNodeMirrors)
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

TEST(DelDomains, TravelIsCapped)
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

TEST(DelDomains, MetalOnOtherTracksDoesNotBlock)
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

TEST(DelDomains, MetalOverlappingTheTrimBlocksIt)
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

TEST(DelDomains, VerticalLayerUsesY)
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

TEST(DelDomains, TrimsOfAGapFromMetal)
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
}  // namespace del
