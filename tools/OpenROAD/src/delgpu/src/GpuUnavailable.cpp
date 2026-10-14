// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <stdexcept>

#include "delgpu/Gpu.h"

namespace delgpu {

GpuInfo initializeGpu()
{
  throw std::runtime_error(
      "DELGPU was built without CUDA. Reconfigure with "
      "bazel build --//src/delgpu:gpu=cuda //:openroad (Bazel) or "
      "-DENABLE_DELGPU_CUDA=ON (CMake).");
}

}  // namespace delgpu
