// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "delgpu/Graph.h"

namespace delgpu {
// CPU stages the BVH and track index; GPU computes domains and sparse edges.
void buildGraphGpu(LayerNodes& layer, const std::vector<odb::Rect>& obstacles);
// Throw on the first differing node domain or canonical edge.
void verifyGraph(const LayerNodes& gpu, const LayerNodes& cpu);
}  // namespace delgpu
