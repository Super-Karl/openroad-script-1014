// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include <vector>

#include "del/Graph.h"
namespace odb {
class dbBlock;
}
namespace del {
bool showOptimizationFrame(odb::dbBlock* block,
                           const std::vector<LayerNodes>& layers,
                           const std::vector<std::vector<double>>& displacement,
                           int iteration,
                           int frame_ms,
                           bool exact = false);
}
