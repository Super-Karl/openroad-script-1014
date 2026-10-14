// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <cstddef>
#include <cstdint>

// Plain data at the nvcc/host compiler boundary; no STL or OpenDB objects.
namespace delgpu {
struct DeviceNode
{
  int64_t s, t, low, up;
  int high_end, blocked;
};
struct DeviceBox
{
  int64_t sl, sh, tl, th;
};
struct DeviceBvh
{
  DeviceBox box;
  int left, right, begin, end;
};
struct DeviceTrack
{
  int64_t t;
  int begin, end;
};
struct DeviceEdge
{
  int src, dst, type;
};
struct DeviceRules
{
  int64_t width, d, dt, du, cap, pitch;
};
}  // namespace delgpu

extern "C" int delgpuBuildCuda(delgpu::DeviceNode* nodes,
                               int node_count,
                               const delgpu::DeviceBox* boxes,
                               int box_count,
                               const delgpu::DeviceBvh* tree,
                               int tree_count,
                               const int* order,
                               const delgpu::DeviceTrack* tracks,
                               int track_count,
                               delgpu::DeviceRules rules,
                               delgpu::DeviceEdge** edges,
                               size_t* edge_count,
                               char* error,
                               size_t error_size);
extern "C" void delgpuFreeEdges(delgpu::DeviceEdge* edges);
