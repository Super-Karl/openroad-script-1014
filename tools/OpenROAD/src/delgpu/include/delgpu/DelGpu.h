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

  void run(bool gui = false,
           bool verify = false,
           bool gpu = true,
           int iterations = kDefaultIterations,
           double max_step = kDefaultMaxStepW,
           double jump_offset_nm = kDefaultJumpOffsetNm,
           int frame_ms = 100,
           const char* report = "");

 private:
  odb::dbBlock* init();

  utl::Logger* logger_;
  bool running_ = false;
};

}  // namespace delgpu
