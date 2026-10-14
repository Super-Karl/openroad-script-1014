// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include <algorithm>
#include <vector>

#include "delgpu/Trim.h"
#include "gtest/gtest.h"

namespace delgpu {
namespace {
void expectTrims(const std::vector<odb::Rect>& metal,
                 bool horizontal,
                 std::vector<odb::Rect> expected)
{
  std::vector<odb::Rect> actual;
  for (const Trim& trim : makeTrimBoxes(metal, 10, horizontal, "M2", 1)) {
    actual.push_back(trim.box);
  }
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(actual, expected);
}

TEST(DelTrimGeometry, HorizontalAndVertical)
{
  expectTrims({{0, 0, 100, 10}}, true, {{-10, -5, 0, 15}, {100, -5, 110, 15}});
  expectTrims({{0, 0, 10, 100}}, false, {{-5, -10, 15, 0}, {-5, 100, 15, 110}});

  // The trim records the metal end it sits on: low coordinate first, high
  // coordinate second, in the preferred direction of the layer.
  const std::vector<Trim> trims
      = makeTrimBoxes({{0, 0, 100, 10}}, 10, true, "M2", 7);
  ASSERT_EQ(trims.size(), 2u);
  EXPECT_EQ(trims[0].box, odb::Rect(-10, -5, 0, 15));
  EXPECT_FALSE(trims[0].high_end);
  EXPECT_EQ(trims[1].box, odb::Rect(100, -5, 110, 15));
  EXPECT_TRUE(trims[1].high_end);
  EXPECT_EQ(trims[0].layer, "M2");
  EXPECT_EQ(trims[0].net_id, 7u);
}

TEST(DelTrimGeometry, OverlapsAndJoinsAreNotEnds)
{
  const std::vector<odb::Rect> expected{{-10, -5, 0, 15}, {100, -5, 110, 15}};
  expectTrims(
      {{0, 0, 50, 10}, {40, 0, 100, 10}, {0, 0, 50, 10}}, true, expected);
  expectTrims({{0, 0, 50, 10}, {50, 0, 100, 10}}, true, expected);
}

TEST(DelTrimGeometry, JogSidesAreNotEnds)
{
  expectTrims({{0, 0, 100, 10}, {40, 10, 50, 50}},
              true,
              {{-10, -5, 0, 15}, {100, -5, 110, 15}});
}

TEST(DelTrimGeometry, HoleSidesAreNotEnds)
{
  expectTrims(
      {{0, 0, 100, 10}, {0, 90, 100, 100}, {0, 0, 10, 100}, {90, 0, 100, 100}},
      true,
      {{-10, -5, 0, 15},
       {-10, 85, 0, 105},
       {100, -5, 110, 15},
       {100, 85, 110, 105}});
}

TEST(DelTrimGeometry, EndInsideHoleFacesEmptySpace)
{
  expectTrims({{0, 0, 100, 10},
               {0, 90, 100, 100},
               {0, 0, 10, 100},
               {90, 0, 100, 100},
               {0, 40, 40, 50}},
              true,
              {{-10, -5, 0, 15},
               {-10, 35, 0, 55},
               {-10, 85, 0, 105},
               {100, -5, 110, 15},
               {100, 85, 110, 105},
               {40, 35, 50, 55}});
}

TEST(DelTrimGeometry, EmptyInput)
{
  expectTrims({}, true, {});
}
}  // namespace
}  // namespace delgpu
