// SPDX-License-Identifier: BSD-3-Clause
#include <cstdio>

#include "delgpu/GraphCuda.h"
extern "C" int delgpuBuildCuda(delgpu::DeviceNode*,
                               int,
                               const delgpu::DeviceBox*,
                               int,
                               const delgpu::DeviceBvh*,
                               int,
                               const int*,
                               const delgpu::DeviceTrack*,
                               int,
                               delgpu::DeviceRules,
                               delgpu::DeviceEdge** edges,
                               size_t* count,
                               char* error,
                               size_t size)
{
  *edges = nullptr;
  *count = 0;
  if (size) {
    std::snprintf(error, size, "CUDA backend was not compiled");
  }
  return 1;
}
extern "C" void delgpuFreeEdges(delgpu::DeviceEdge*)
{
}
