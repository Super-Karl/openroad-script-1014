// SPDX-License-Identifier: BSD-3-Clause
#pragma once
namespace delgpu {
inline constexpr int kDefaultIterations = 150;
inline constexpr double kDefaultMaxStepW = 0.25;
inline constexpr double kDefaultJumpOffsetNm = 35.0;
// Zero retries: test the initial step, without step-halving backtracking.
inline constexpr int kDefaultMaxBacktracks = 0;
inline constexpr double kDefaultLearningRate = 1.5;
inline constexpr double kDefaultLearningRateEnd = 0.5;
inline constexpr char kDefaultLearningRateMode[] = "adaptive";
}  // namespace delgpu
