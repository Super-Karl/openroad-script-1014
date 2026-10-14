// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

#include <cstddef>
#include <string>

namespace delgpu {

struct GpuInfo
{
  int device;
  std::string name;
  std::size_t total_memory;
  int major;
  int minor;
  int multiprocessors;
  int driver_version;
  int runtime_version;
};

// Initialize the default CUDA device and return its properties, or throw.
GpuInfo initializeGpu();

}  // namespace delgpu
