// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "UnrepairedGraphics.h"
#include "gtest/gtest.h"

namespace del {
TEST(DelUnrepairedGraphics, ThresholdAndContinuousBounds)
{
  LayerNodes layer;
  layer.rules = makeLayerRules(10);
  Vnode a, b;
  a.id = 0;
  a.trim = odb::Rect(-5, -5, 5, 5);
  b.id = 1;
  b.center_x2 = 90;
  b.trim = odb::Rect(40, -5, 50, 5);
  layer.nodes = {a, b};
  Arc arc;
  arc.src = 0;
  arc.dst = 1;
  arc.ranges = {{.low2 = 100, .has_high = false}};
  // Gap is exactly 5 DBU; translating both ends must retain the violation.
  const auto box = unrepairedArcBounds(layer, arc, {0.25, 0.25}, 1000);
  ASSERT_TRUE(box);
  EXPECT_EQ(box->xMin(), -5);
  EXPECT_EQ(box->xMax(), 51);
  EXPECT_EQ(box->yMin(), -5);
  EXPECT_EQ(box->yMax(), 5);
  EXPECT_FALSE(unrepairedArcBounds(layer, arc, {0, 1}, 1000));
  // Final integer postprocess must display a 1 DBU residual, even below 5 nm.
  EXPECT_TRUE(unrepairedArcBounds(layer, arc, {0, 4}, 1000, true));
  EXPECT_FALSE(unrepairedArcBounds(layer, arc, {0, 5}, 1000, true));
  EXPECT_FALSE(unrepairedArcBounds(layer, arc, {0, 0}, 2000));
  EXPECT_FALSE(unrepairedArcBounds(layer, arc, {0, 6}, 1000));
  layer.horizontal = false;
  layer.nodes[1].center_x2 = 0;
  layer.nodes[1].center_y2 = 90;
  layer.nodes[1].trim = odb::Rect(-5, 40, 5, 50);
  const auto vertical = unrepairedArcBounds(layer, arc, {0.25, 0.25}, 1000);
  ASSERT_TRUE(vertical);
  EXPECT_EQ(vertical->xMax(), 5);
  EXPECT_EQ(vertical->yMax(), 51);
}
}  // namespace del
