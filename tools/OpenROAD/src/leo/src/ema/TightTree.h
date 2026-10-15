// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once
#include <cstdint>
#include <vector>

#include "leo/Graph.h"
namespace leo {
struct MoveCandidate
{
  int direction = 0;
  int64_t step2 = 0;
  int64_t cost2 = 0;
  std::vector<int> nodes;
};
// Construct a fresh directional tight tree, then limit its step by all domains
// and crossing constraints. No adjacency or membership state survives a call.
MoveCandidate evaluateMove(const LayerNodes& layer,
                           const std::vector<Constraint>& constraints,
                           const std::vector<int64_t>& value,
                           int pivot,
                           int direction);
}  // namespace leo
