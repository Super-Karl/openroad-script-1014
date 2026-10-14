// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <vector>

#include "del/Graph.h"

namespace odb {
class dbBlock;
}

namespace del {
// One display layer per routing layer, drawing the trim centers of its Vnodes.
void showVnodes(odb::dbBlock* block, const std::vector<LayerNodes>& layers);

// One display layer per routing layer and rule type, named <layer>_<rule>,
// drawing the arcs built by that rule.
void showArcs(odb::dbBlock* block, const std::vector<LayerNodes>& layers);

}  // namespace del
