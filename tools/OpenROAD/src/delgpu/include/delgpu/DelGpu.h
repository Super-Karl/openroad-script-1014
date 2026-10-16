// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include "delgpu/Parameters.h"

namespace odb {
class dbBlock;
}

namespace utl {
class Logger;
}

namespace delgpu {

// OpenROAD owns this tool; CUDA initialization is deferred until run().
class DelGpu
{
 public:
  explicit DelGpu(utl::Logger* logger);

  // C++ end=0 selects the mode's default; Tcl accepts explicit end rates >0.
  void run(bool gui = false,
           bool verify = false,
           bool gpu = true,
           int iterations = kDefaultIterations,
           double max_step = kDefaultMaxStepW,
           double jump_offset_nm = kDefaultJumpOffsetNm,
           int frame_ms = 100,
           const char* report = "",
           int max_backtracks = kDefaultMaxBacktracks,
           double learning_rate = kDefaultLearningRate,
           double learning_rate_end = kDefaultLearningRateEnd,
           const char* learning_rate_mode = kDefaultLearningRateMode,
           bool postprocess = true,
           int post_sweeps = 24,
           int post_pair_rounds = 8,
           int post_retract_sweeps = 8,
           const char* post_retract_mode = kDefaultPostRetractMode,
           int post_joint_rounds = 0,
           int post_joint_trials = 16,
           double wire_weight = kDefaultWireWeight,
           bool postprocess_only = false);

 private:
  odb::dbBlock* init();

  utl::Logger* logger_;
  bool running_ = false;
};

}  // namespace delgpu
