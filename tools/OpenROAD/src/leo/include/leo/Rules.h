// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace leo {

// ASAP7 constraint graph rules. The distance between two Vnodes is measured
// between the centers of their trims, so these thresholds are compared against
// trim center distances. D belongs to A arcs (minimum spacing or minimum metal
// length on a track), Dt to same-track B arcs and Du to adjacent-track C arcs.
// The multipliers are the tuning point of the ASAP7 research model.
constexpr int64_t kDWidthMultiplier = 5;   // D = 5 * layer width
constexpr int64_t kDtWidthMultiplier = 5;  // Dt = 5 * layer width
constexpr int64_t kDuWidthMultiplier = 3;  // Du = 3 * layer width
constexpr int kDefaultPruneFactor = 3;     // P = k * Dt (paper section 4.1)

struct LayerRules
{
  int width = 0;           // dbTechLayer::getWidth() in DBU
  int64_t d = 0;           // kDWidthMultiplier * width
  int64_t dt = 0;          // kDtWidthMultiplier * width
  int64_t du = 0;          // kDuWidthMultiplier * width
  int64_t move_limit = 0;  // search travel bound; initially prune_factor * dt
};

// Every layer uses its own basic width for D, Dt, Du and the travel cap.
constexpr LayerRules makeLayerRules(int width,
                                    int prune_factor = kDefaultPruneFactor)
{
  if (prune_factor <= 0 || width < 0
      || kDtWidthMultiplier * width
             > std::numeric_limits<int64_t>::max() / 2 / prune_factor) {
    throw std::invalid_argument(
        "LEO P=k*Dt must be positive and representable");
  }
  return LayerRules{
      .width = width,
      .d = kDWidthMultiplier * width,
      .dt = kDtWidthMultiplier * width,
      .du = kDuWidthMultiplier * width,
      .move_limit = int64_t{prune_factor} * kDtWidthMultiplier * width};
}

}  // namespace leo
