// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "leo/Graph.h"

namespace odb {
class dbBlock;
}

namespace leo {
// One display layer per routing layer, drawing the trim centers of its Vnodes.
void showVnodes(odb::dbBlock* block, const std::vector<LayerNodes>& layers);

// One display layer per routing layer and rule type, named <layer>/arcs/<rule>,
// drawing the arcs built by that rule.
void showArcs(odb::dbBlock* block, const std::vector<LayerNodes>& layers);

// One display layer per routing layer under <layer>/moves: the trim where LPA
// put it, with an arrow from the initial center for every moved node.
void showMoves(odb::dbBlock* block, const std::vector<LayerNodes>& layers);

}  // namespace leo
