// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <vector>

#include "leo/Graph.h"

namespace leo {

// Outcome of the EMA stage for one subgraph.
struct EmaResult
{
  enum class Status
  {
    kUnchanged,       // no move improved the extent length
    kImproved,        // the extent length went down
    kIterationLimit,  // stopped by the explicit operation limit
  };

  size_t nodes = 0;
  size_t arcs = 0;
  size_t iterations = 0;
  size_t moves = 0;  // number of committed moves
  int64_t length_before2 = 0;
  int64_t length_after2 = 0;
  Status status = Status::kUnchanged;
};

// Extent minimization of the paper (§4.2.2) over every weak connected component
// of the layer. It starts from the LPA solution and repeatedly moves the tight
// closure of a pivot node as a group: the closure holds the nodes that must
// travel together to keep the tight arcs satisfied, and the step is limited by
// the domains and by the arcs crossing the closure. Only a move that shortens
// the total extent, sum of the weights times the travelled distance, is
// committed, so every step strictly reduces an integer objective.
//
// Fills Vnode::pos_x2/pos_y2 and LayerNodes::ema, and returns one result per
// subgraph, in the component order.
std::vector<EmaResult> solveEma(LayerNodes& layer);

}  // namespace leo
