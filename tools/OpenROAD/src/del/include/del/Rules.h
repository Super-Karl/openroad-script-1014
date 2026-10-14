// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>

namespace del {

// ASAP7 constraint graph rules. The distance between two Vnodes is measured
// between the centers of their trims, so these thresholds are compared against
// trim center distances. D belongs to A arcs (minimum spacing or minimum metal
// length on a track), Dt to same-track B arcs and Du to adjacent-track C arcs.
// The multipliers are the tuning point of the ASAP7 research model.
constexpr int64_t kDWidthMultiplier = 5;           // D = 5 * layer width
constexpr int64_t kDtWidthMultiplier = 5;          // Dt = 5 * layer width
constexpr int64_t kDuWidthMultiplier = 3;          // Du = 3 * layer width
constexpr int64_t kMoveLimitWidthMultiplier = 20;  // travel cap = 20 * width

struct LayerRules
{
  int width = 0;           // dbTechLayer::getWidth() in DBU
  int64_t d = 0;           // kDWidthMultiplier * width
  int64_t dt = 0;          // kDtWidthMultiplier * width
  int64_t du = 0;          // kDuWidthMultiplier * width
  int64_t move_limit = 0;  // kMoveLimitWidthMultiplier * width
};

// Every layer uses its own basic width for D, Dt, Du and the travel cap.
constexpr LayerRules makeLayerRules(int width)
{
  return LayerRules{.width = width,
                    .d = kDWidthMultiplier * width,
                    .dt = kDtWidthMultiplier * width,
                    .du = kDuWidthMultiplier * width,
                    .move_limit = kMoveLimitWidthMultiplier * width};
}

}  // namespace del
