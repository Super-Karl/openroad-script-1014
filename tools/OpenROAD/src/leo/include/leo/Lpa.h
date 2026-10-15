// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <vector>

#include "leo/Graph.h"

namespace leo {

// Outcome of one subgraph, for the log of the LPA stage.
struct SubgraphResult
{
  enum class Status
  {
    kSolved,         // every arc got a feasible range
    kPartial,        // at least one arc has no feasible range
    kInvalidDomain,  // a node domain is empty, the subgraph is not solved
  };

  size_t nodes = 0;
  size_t arcs = 0;
  size_t unresolved_arcs = 0;
  size_t moved = 0;
  int64_t added_length2 = 0;  // doubled, in DBU
  Status status = Status::kSolved;
};

// Longest path based algorithm of the paper over every weak connected component
// of the layer: order the arcs by rule and relaxation slack, try for each arc
// the range closest to the ideal relative position of its two nodes, and keep
// it when the difference constraints stay feasible. Positions start at the
// ideal position of each node, so a trim only travels when a constraint forces
// it outwards.
//
// Fills Arc::selected_range, Vnode::pos_x2/pos_y2 and LayerNodes::lpa, and
// returns one result per subgraph, in the component order.
std::vector<SubgraphResult> solveLpa(LayerNodes& layer);

}  // namespace leo
