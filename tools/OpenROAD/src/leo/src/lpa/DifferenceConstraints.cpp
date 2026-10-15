// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "DifferenceConstraints.h"

#include <cstddef>

namespace leo {

FeasibleBounds solveDifferenceConstraints(
    const std::vector<int64_t>& low,
    const std::vector<int64_t>& up,
    const std::vector<Constraint>& constraints)
{
  FeasibleBounds result{false, low, up};
  for (size_t i = 0; i < low.size(); ++i) {
    if (low[i] > up[i]) {
      return result;
    }
  }
  // Full Bellman-Ford scans in each direction. A change on scan |V| proves
  // a positive cycle; an exceeded domain bound also proves infeasibility.
  for (size_t pass = 0; pass < low.size(); ++pass) {
    bool changed = false;
    for (const Constraint& c : constraints) {
      const int64_t value = result.lower[c.src] + c.distance;
      if (value > result.lower[c.dst]) {
        result.lower[c.dst] = value;
        changed = true;
        if (value > up[c.dst]) {
          return result;
        }
      }
    }
    if (!changed) {
      break;
    }
    if (pass + 1 == low.size()) {
      return result;
    }
  }
  for (size_t pass = 0; pass < low.size(); ++pass) {
    bool changed = false;
    for (const Constraint& c : constraints) {
      const int64_t value = result.upper[c.dst] - c.distance;
      if (value < result.upper[c.src]) {
        result.upper[c.src] = value;
        changed = true;
        if (value < low[c.src]) {
          return result;
        }
      }
    }
    if (!changed) {
      break;
    }
    if (pass + 1 == low.size()) {
      return result;
    }
  }
  result.feasible = true;
  return result;
}

}  // namespace leo
