// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "TightTree.h"

#include <algorithm>
#include <cstdint>
#include <limits>
namespace leo {
MoveCandidate evaluateMove(const LayerNodes& layer,
                           const std::vector<Constraint>& constraints,
                           const std::vector<int64_t>& value,
                           int pivot,
                           int direction)
{
  MoveCandidate move;
  move.direction = direction;
  move.nodes.push_back(pivot);
  std::vector<bool> member(layer.nodes.size(), false);
  member[pivot] = true;
  int64_t weight = 0;
  for (size_t index = 0; index < move.nodes.size(); ++index) {
    const int node = move.nodes[index];
    weight += layer.nodes[node].high_end ? 1 : -1;
    // Revisit every constraint; no retained adjacency or slack cache.
    for (const Constraint& c : constraints) {
      if ((direction > 0 ? c.src : c.dst) != node) {
        continue;
      }
      const int next = direction > 0 ? c.dst : c.src;
      if (!member[next] && value[c.dst] - value[c.src] == c.distance) {
        member[next] = true;
        move.nodes.push_back(next);
      }
    }
  }
  int64_t step = std::numeric_limits<int64_t>::max();
  for (int id : move.nodes) {
    const Vnode& node = layer.nodes[id];
    step = std::min(
        step, direction > 0 ? node.up2 - value[id] : value[id] - node.low2);
  }
  for (const Constraint& c : constraints) {
    if ((direction > 0 && member[c.src] && !member[c.dst])
        || (direction < 0 && member[c.dst] && !member[c.src])) {
      step = std::min(step, value[c.dst] - value[c.src] - c.distance);
    }
  }
  if (step > 0 && direction * weight < 0) {
    move.step2 = step;
    move.cost2 = direction * weight * step;
  }
  return move;
}
}  // namespace leo
