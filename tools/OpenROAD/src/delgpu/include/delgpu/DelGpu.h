// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

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

  void run();

 private:
  odb::dbBlock* init();

  utl::Logger* logger_;
};

}  // namespace delgpu
