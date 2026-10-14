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

// Layer width 10 gives D = Dt = 50, Du = 30 and a travel cap of 200; the pitch
// equals 2 * width, as in ASAP7 at these layers.
constexpr int kWidth = 10;
constexpr int64_t kPitch = 2 * kWidth;

// A node on a horizontal layer. The travel is given in doubled DBU, 0 for a
// node that cannot move at all.
Vnode node(int id,
           int64_t center_x2,
           int64_t center_y2,
           bool high_end,
           int64_t l_move2 = 0,
           int64_t u_move2 = 0)
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
  return node;
}

LayerNodes layerWith(std::vector<Vnode> nodes)
{
  LayerNodes layer;
  layer.layer = "M2";
  layer.horizontal = true;
  layer.pitch = kPitch;
  layer.rules = makeLayerRules(kWidth);
  layer.nodes = std::move(nodes);
  return layer;
}

size_t countOf(const LayerNodes& layer, ArcType type)
{
  size_t count = 0;
  for (const Arc& arc : layer.arcs) {
    count += arc.type == type ? 1 : 0;
  }
  return count;
}

TEST(DelArcs, FacingTrimsMayMerge)
{
  LayerNodes layer = layerWith({node(0, 0, 0, true), node(1, 60, 0, false)});
  buildArcs(layer);

  ASSERT_EQ(layer.arcs.size(), 1u);
  const Arc& arc = layer.arcs[0];
  EXPECT_EQ(arc.id, 0);
  EXPECT_EQ(arc.type, ArcType::kB);
  EXPECT_EQ(arc.src, 0);
  EXPECT_EQ(arc.dst, 1);
  // [0, 0] u [Dt, +inf) in doubled DBU.
  ASSERT_EQ(arc.ranges.size(), 2u);
  EXPECT_EQ(arc.ranges[0].low2, 0);
  EXPECT_EQ(arc.ranges[0].high2, 0);
  EXPECT_EQ(arc.ranges[1].low2, 2 * layer.rules.dt);
  EXPECT_FALSE(arc.ranges[1].has_high);
}

TEST(DelArcs, SameTrackKeepsSeparation)
{
  // The two trims do not face each other, so they may not merge.
  LayerNodes layer = layerWith({node(0, 0, 0, false), node(1, 60, 0, true)});
  buildArcs(layer);

  ASSERT_EQ(layer.arcs.size(), 1u);
  const Arc& arc = layer.arcs[0];
  EXPECT_EQ(arc.type, ArcType::kA);
  // [D, +inf) in doubled DBU.
  ASSERT_EQ(arc.ranges.size(), 1u);
  EXPECT_EQ(arc.ranges[0].low2, 2 * layer.rules.d);
  EXPECT_FALSE(arc.ranges[0].has_high);
}

TEST(DelArcs, AdjacentTrackTrims)
{
  // One track apart (dt = pitch), 20 DBU apart along the track.
  LayerNodes layer
      = layerWith({node(0, 0, 0, true), node(1, 20, 2 * kPitch, false)});
  buildArcs(layer);

  ASSERT_EQ(layer.arcs.size(), 1u);
  const Arc& arc = layer.arcs[0];
  EXPECT_EQ(arc.type, ArcType::kC);
  // (-inf, -Du] u [0, 0] u [Du, +inf) in doubled DBU.
  ASSERT_EQ(arc.ranges.size(), 3u);
  EXPECT_FALSE(arc.ranges[0].has_low);
  EXPECT_EQ(arc.ranges[0].high2, -2 * layer.rules.du);
  EXPECT_EQ(arc.ranges[1].low2, 0);
  EXPECT_EQ(arc.ranges[1].high2, 0);
  EXPECT_EQ(arc.ranges[2].low2, 2 * layer.rules.du);
  EXPECT_FALSE(arc.ranges[2].has_high);
}

TEST(DelArcs, PairsOutsideTheRuleDistance)
{
  // Same track, already separated by Dt and unable to move.
  LayerNodes same_track
      = layerWith({node(0, 0, 0, true), node(1, 2 * 50, 0, false)});
  buildArcs(same_track);
  EXPECT_TRUE(same_track.arcs.empty());

  // Adjacent track, already separated by Du and unable to move.
  LayerNodes adjacent
      = layerWith({node(0, 0, 0, true), node(1, 2 * 30, 2 * kPitch, false)});
  buildArcs(adjacent);
  EXPECT_TRUE(adjacent.arcs.empty());

  // Two tracks away.
  LayerNodes two_tracks
      = layerWith({node(0, 0, 0, true), node(1, 0, 4 * kPitch, false)});
  buildArcs(two_tracks);
  EXPECT_TRUE(two_tracks.arcs.empty());

  // Exactly one track away and aligned: merging is legal, so no arc is needed.
  LayerNodes aligned
      = layerWith({node(0, 0, 0, true), node(1, 0, 2 * kPitch, true)});
  buildArcs(aligned);
  EXPECT_TRUE(aligned.arcs.empty());

  // The same pair gets an arc once a node can travel into the illegal band.
  LayerNodes reachable
      = layerWith({node(0, 0, 0, true), node(1, 0, 2 * kPitch, false, 20, 0)});
  buildArcs(reachable);
  EXPECT_EQ(countOf(reachable, ArcType::kC), 1u);
}

TEST(DelArcs, TravelWithinTheDomainDecides)
{
  // Same track, 20 W apart (already legal), but both nodes may travel far
  // enough to reach the illegal band between merge and Dt.
  LayerNodes layer = layerWith(
      {node(0, 0, 0, true, 0, 200), node(1, 400, 0, false, 200, 0)});
  buildArcs(layer);
  EXPECT_EQ(countOf(layer, ArcType::kB), 1u);

  // With less travel the pair can never leave the legal separation, so there
  // is no arc even though the pair is inside the search window.
  LayerNodes out_of_reach
      = layerWith({node(0, 0, 0, true, 0, 40), node(1, 400, 0, false, 40, 0)});
  buildArcs(out_of_reach);
  EXPECT_TRUE(out_of_reach.arcs.empty());
}

TEST(DelArcs, IdsFollowTypeAndEndpoints)
{
  LayerNodes layer = layerWith({node(0, 0, 0, true, 10, 10),
                                node(1, 60, 0, false, 10, 10),
                                node(2, 0, 2 * kPitch, true, 10, 10),
                                node(3, 20, 2 * kPitch, false, 10, 10)});
  buildArcs(layer);

  // Two same-track merge pairs and the cross-track pairs that can travel into
  // the illegal band between merge and Du; the pair aligned at ds = 0 stays
  // legal.
  ASSERT_EQ(layer.arcs.size(), 6u);
  EXPECT_EQ(countOf(layer, ArcType::kB), 2u);
  EXPECT_EQ(countOf(layer, ArcType::kC), 4u);

  // Ids are dense and ordered by rule, then by endpoints.
  EXPECT_EQ(layer.arcs[0].type, ArcType::kB);
  EXPECT_EQ(layer.arcs[0].src, 0);
  EXPECT_EQ(layer.arcs[0].dst, 1);
  EXPECT_EQ(layer.arcs[1].type, ArcType::kB);
  EXPECT_EQ(layer.arcs[1].src, 2);
  EXPECT_EQ(layer.arcs[1].dst, 3);
  EXPECT_EQ(layer.arcs[2].type, ArcType::kC);
  EXPECT_EQ(layer.arcs[2].src, 0);
  EXPECT_EQ(layer.arcs[2].dst, 2);
  EXPECT_EQ(layer.arcs[3].type, ArcType::kC);
  EXPECT_EQ(layer.arcs[3].src, 0);
  EXPECT_EQ(layer.arcs[3].dst, 3);
  EXPECT_EQ(layer.arcs[4].type, ArcType::kC);
  EXPECT_EQ(layer.arcs[4].src, 1);
  EXPECT_EQ(layer.arcs[4].dst, 2);
  EXPECT_EQ(layer.arcs[5].type, ArcType::kC);
  EXPECT_EQ(layer.arcs[5].src, 1);
  EXPECT_EQ(layer.arcs[5].dst, 3);
  for (size_t i = 0; i < layer.arcs.size(); ++i) {
    EXPECT_EQ(layer.arcs[i].id, static_cast<int>(i));
  }
}

TEST(DelArcs, TrimsAcrossAGapMayMerge)
{
  // Two wires of one net end 20 DBU apart on the same track: the two trims
  // face each other inside the gap and may merge.
  const std::vector<odb::Rect> metal{{0, 0, 40, 10}, {60, 0, 100, 10}};
  const std::vector<Trim> trims = makeTrimBoxes(metal, kWidth, true, "M2", 1);
  LayerNodes layer = layerWith(makeVnodes(trims, 0, trims.size(), true));
  computeDomains(layer, metal);
  buildArcs(layer);

  ASSERT_EQ(layer.nodes.size(), 4u);
  ASSERT_EQ(layer.arcs.size(), 1u);
  EXPECT_EQ(layer.arcs[0].type, ArcType::kB);
  // The arc joins the trims of the two facing ends inside the gap.
  EXPECT_EQ(center(layer.nodes[layer.arcs[0].src]).x(), 45);
  EXPECT_EQ(center(layer.nodes[layer.arcs[0].dst]).x(), 55);
}

TEST(DelArcs, EmptyLayer)
{
  LayerNodes layer;
  buildArcs(layer);
  EXPECT_TRUE(layer.arcs.empty());
}

}  // namespace
}  // namespace del
