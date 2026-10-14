// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <cuda_runtime.h>

#include <stdexcept>
#include <string>

#include "delgpu/Gpu.h"

namespace delgpu {
namespace {

void checkCuda(cudaError_t status, const char* operation)
{
  if (status != cudaSuccess) {
    throw std::runtime_error(std::string(operation) + ": "
                             + cudaGetErrorString(status));
  }
}

}  // namespace

GpuInfo initializeGpu()
{
  int count = 0;
  checkCuda(cudaGetDeviceCount(&count), "cudaGetDeviceCount");
  if (count == 0) {
    throw std::runtime_error("No CUDA device is available");
  }
  checkCuda(cudaSetDevice(0), "cudaSetDevice");
  checkCuda(cudaFree(nullptr), "CUDA context initialization");
  checkCuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");

  int device = 0;
  checkCuda(cudaGetDevice(&device), "cudaGetDevice");
  cudaDeviceProp properties{};
  checkCuda(cudaGetDeviceProperties(&properties, device),
            "cudaGetDeviceProperties");
  int driver_version = 0;
  int runtime_version = 0;
  checkCuda(cudaDriverGetVersion(&driver_version), "cudaDriverGetVersion");
  checkCuda(cudaRuntimeGetVersion(&runtime_version), "cudaRuntimeGetVersion");
  return {device,
          properties.name,
          properties.totalGlobalMem,
          properties.major,
          properties.minor,
          properties.multiProcessorCount,
          driver_version,
          runtime_version};
}

}  // namespace delgpu
