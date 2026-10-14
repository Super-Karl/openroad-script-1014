// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "delgpu/Graph.h"
namespace odb {
class dbBlock;
}
namespace delgpu {
void clearGraphGraphics();
void showGraphGraphics(odb::dbBlock* block, std::vector<LayerNodes>&& layers);
void updateGraphGraphics(size_t layer, std::vector<double>&& displacement);
}  // namespace delgpu
