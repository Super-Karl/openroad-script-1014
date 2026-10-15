// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "delgpu/Graph.h"
#include "delgpu/Parameters.h"
#include "delgpu/TrimOptimizer.h"
namespace delgpu {
PostprocessGraph makePostprocessGraph(const LayerNodes& layer,
                                      int dbu_per_micron);
PostprocessStats assessIntegerDisplacement(
    const LayerNodes& layer,
    const std::vector<double>& displacement,
    int dbu_per_micron);
std::unique_ptr<TrimOptimizer> makeGpuOptimizer(
    const LayerNodes& layer,
    double tolerance_dbu,
    double max_step,
    double dbu_per_micron = 1000.0,
    double jump_offset_nm = kDefaultJumpOffsetNm,
    int max_backtracks = kDefaultMaxBacktracks,
    double learning_rate = kDefaultLearningRate);
// Recompute final quality and assert hard domains on the host snapshot.
TrimStats assessDisplacement(const LayerNodes& layer,
                             const std::vector<double>& displacement,
                             double tolerance_dbu);
}  // namespace delgpu
