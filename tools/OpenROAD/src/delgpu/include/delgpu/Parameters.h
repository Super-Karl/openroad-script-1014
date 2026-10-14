// SPDX-License-Identifier: BSD-3-Clause
#pragma once
namespace delgpu {
inline constexpr int kDefaultIterations = 200;
inline constexpr double kDefaultMaxStepW = 0.25;
inline constexpr double kDefaultJumpOffsetNm = 35.0;
// Zero retries: test the initial step, without step-halving backtracking.
inline constexpr int kDefaultMaxBacktracks = 0;
}  // namespace delgpu
