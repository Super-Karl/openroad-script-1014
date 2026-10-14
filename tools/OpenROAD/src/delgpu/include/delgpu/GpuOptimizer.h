// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "delgpu/Graph.h"
#include "delgpu/Parameters.h"
#include "delgpu/TrimOptimizer.h"
namespace delgpu {
std::unique_ptr<TrimOptimizer> makeGpuOptimizer(const LayerNodes& layer,
                                                double tolerance_dbu,
                                                double max_step,
                                                double dbu_per_micron = 1000.0,
                                                double jump_offset_nm
                                                = kDefaultJumpOffsetNm,
                                                int max_backtracks
                                                = kDefaultMaxBacktracks);
// Recompute final quality and assert hard domains on the host snapshot.
TrimStats assessDisplacement(const LayerNodes& layer,
                             const std::vector<double>& displacement,
                             double tolerance_dbu);
}  // namespace delgpu
