// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once
#include <cstdint>
#include <vector>

#include "leo/Graph.h"
namespace leo {
struct FeasibleBounds
{
  bool feasible = false;
  std::vector<int64_t> lower;
  std::vector<int64_t> upper;
};
// A fresh longest-path solve from the domain bounds. No retained propagation
// state, trial rollback, adjacency cache, or warm start is used.
FeasibleBounds solveDifferenceConstraints(
    const std::vector<int64_t>& low,
    const std::vector<int64_t>& up,
    const std::vector<Constraint>& constraints);
}  // namespace leo
