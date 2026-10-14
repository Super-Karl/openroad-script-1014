// SPDX-License-Identifier: BSD-3-Clause
#include <cuda_runtime.h>

#include <cstdio>
#include <cstdlib>
#include <cub/device/device_scan.cuh>
#include <limits>
#include <stdexcept>
#include <string>

#include "delgpu/GraphCuda.h"

namespace {
using namespace delgpu;
using Count = unsigned long long;

void check(cudaError_t status)
{
  if (status != cudaSuccess) {
    throw std::runtime_error(cudaGetErrorString(status));
  }
}

template <typename T>
class Buffer
{
 public:
  explicit Buffer(size_t count) : data_(nullptr)
  {
    if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
      throw std::overflow_error("CUDA buffer size overflow");
    }
    if (count) {
      check(cudaMalloc(reinterpret_cast<void**>(&data_), count * sizeof(T)));
    }
  }
  ~Buffer()
  {
    if (data_) {
      cudaFree(data_);
    }
  }
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  T* get() const { return data_; }
  void upload(const T* data, size_t count)
  {
    if (count) {
      check(cudaMemcpy(data_, data, count * sizeof(T), cudaMemcpyHostToDevice));
    }
  }

 private:
  T* data_;
};

__device__ int64_t smaller(int64_t a, int64_t b)
{
  return a < b ? a : b;
}
__device__ int64_t larger(int64_t a, int64_t b)
{
  return a > b ? a : b;
}

__global__ void domains(DeviceNode* nodes,
                        int n,
                        const DeviceBox* boxes,
                        const DeviceBvh* tree,
                        int tree_count,
                        DeviceRules r)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) {
    return;
  }
  DeviceNode& node = nodes[i];
  const int64_t bl = node.s - r.width;
  const int64_t bh = node.s + r.width;
  const int64_t tl = node.t - 2 * r.width;
  const int64_t th = node.t + 2 * r.width;
  const int64_t cap = 2 * r.cap;
  int64_t below = bl - cap, above = bh + cap;
  bool blocked = false;
  // Median splitting bounds depth by log2(INT_MAX), safely below 64.
  int stack[64];
  int top = 0;
  if (tree_count) {
    stack[top++] = 0;
  }
  while (top) {
    const DeviceBvh branch = tree[stack[--top]];
    const DeviceBox bounds = branch.box;
    if (bounds.sh < bl - cap || bounds.sl > bh + cap || bounds.th <= tl
        || bounds.tl >= th) {
      continue;
    }
    if (branch.left >= 0) {
      stack[top++] = branch.right;
      stack[top++] = branch.left;
      continue;
    }
    for (int j = branch.begin; j < branch.end; ++j) {
      const DeviceBox box = boxes[j];
      if (box.th <= tl || box.tl >= th || box.sh < bl - cap
          || box.sl > bh + cap) {
        continue;
      }
      if (box.sl < bh && box.sh > bl) {
        blocked = true;
      }
      if (box.sh <= bl) {
        below = larger(below, box.sh);
      }
      if (box.sl >= bh) {
        above = smaller(above, box.sl);
      }
    }
  }
  node.blocked = blocked;
  node.low = blocked ? node.s : node.s - (bl - below);
  node.up = blocked ? node.s : node.s + (above - bh);
}

__device__ bool covered(int type, int64_t lo, int64_t hi, DeviceRules r)
{
  if (lo > hi) {
    return true;
  }
  if (type == 0) {
    return lo >= 2 * r.d;
  }
  if (type == 1) {
    return (lo == 0 && hi == 0) || lo >= 2 * r.dt;
  }
  return hi <= -2 * r.du || (lo == 0 && hi == 0) || lo >= 2 * r.du;
}

__device__ int lowerS(const DeviceNode* nodes,
                      const int* order,
                      int begin,
                      int end,
                      int64_t s)
{
  while (begin < end) {
    const int mid = begin + (end - begin) / 2;
    if (nodes[order[mid]].s < s) {
      begin = mid + 1;
    } else {
      end = mid;
    }
  }
  return begin;
}

// Two passes share exactly the same predicate: count, scan, then write.
// Track indices plus binary searches avoid a quadratic all-pairs search.
__global__ void arcs(const DeviceNode* nodes,
                     int n,
                     const int* order,
                     const DeviceTrack* tracks,
                     int nt,
                     DeviceRules r,
                     Count* counts,
                     const Count* offsets,
                     DeviceEdge* edges,
                     bool write)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) {
    return;
  }
  const int src = order[i];
  const DeviceNode a = nodes[src];
  int begin = 0, end = nt;
  while (begin < end) {
    const int mid = begin + (end - begin) / 2;
    if (tracks[mid].t < a.t) {
      begin = mid + 1;
    } else {
      end = mid;
    }
  }
  const int own_track = begin;
  Count count = 0;
  for (int k = own_track; k < nt; ++k) {
    const bool same = k == own_track;
    if (!same && (r.pitch <= 0 || tracks[k].t - a.t > 2 * r.pitch)) {
      break;
    }
    const int64_t window
        = same ? 2 * larger(r.d, r.dt) + 4 * r.cap : 2 * r.du + 4 * r.cap;
    const int first = same ? i + 1
                           : lowerS(nodes,
                                    order,
                                    tracks[k].begin,
                                    tracks[k].end,
                                    a.s - window + 1);
    for (int j = first; j < tracks[k].end; ++j) {
      const int dst = order[j];
      const DeviceNode b = nodes[dst];
      if (b.s - a.s >= window) {
        break;
      }
      const int type = same ? (a.high_end && !b.high_end ? 1 : 0) : 2;
      if (covered(type, b.low - a.up, b.up - a.low, r)) {
        continue;
      }
      if (write) {
        edges[offsets[i] + count] = {src, dst, type};
      }
      ++count;
    }
  }
  if (!write) {
    counts[i] = count;
  }
}
}  // namespace

extern "C" int delgpuBuildCuda(delgpu::DeviceNode* nodes,
                               int n,
                               const delgpu::DeviceBox* boxes,
                               int nb,
                               const delgpu::DeviceBvh* tree,
                               int nbt,
                               const int* order,
                               const delgpu::DeviceTrack* tracks,
                               int nt,
                               delgpu::DeviceRules rules,
                               delgpu::DeviceEdge** edges,
                               size_t* edge_count,
                               char* error,
                               size_t error_size)
{
  *edges = nullptr;
  *edge_count = 0;
  try {
    if (n <= 0) {
      return 0;
    }
    if (n == std::numeric_limits<int>::max()) {
      throw std::overflow_error("Too many nodes");
    }
    Buffer<DeviceNode> dn(n);
    Buffer<DeviceBox> db(nb);
    Buffer<DeviceBvh> dtree(nbt);
    Buffer<int> dorder(n);
    Buffer<DeviceTrack> dtracks(nt);
    Buffer<Count> counts(size_t(n) + 1), offsets(size_t(n) + 1);
    dn.upload(nodes, n);
    db.upload(boxes, nb);
    dtree.upload(tree, nbt);
    dorder.upload(order, n);
    dtracks.upload(tracks, nt);
    const int blocks = (n - 1) / 128 + 1;
    domains<<<blocks, 128>>>(dn.get(), n, db.get(), dtree.get(), nbt, rules);
    check(cudaGetLastError());
    check(cudaMemset(counts.get(), 0, (size_t(n) + 1) * sizeof(Count)));
    arcs<<<blocks, 128>>>(dn.get(),
                          n,
                          dorder.get(),
                          dtracks.get(),
                          nt,
                          rules,
                          counts.get(),
                          nullptr,
                          nullptr,
                          false);
    check(cudaGetLastError());
    size_t temp_size = 0;
    check(cub::DeviceScan::ExclusiveSum(
        nullptr, temp_size, counts.get(), offsets.get(), n + 1));
    Buffer<unsigned char> temp(temp_size);
    check(cub::DeviceScan::ExclusiveSum(
        temp.get(), temp_size, counts.get(), offsets.get(), n + 1));
    Count total = 0;
    check(cudaMemcpy(
        &total, offsets.get() + n, sizeof(total), cudaMemcpyDeviceToHost));
    if (total > static_cast<Count>(std::numeric_limits<int>::max())) {
      throw std::overflow_error("Too many graph edges");
    }
    Buffer<DeviceEdge> de(total);
    if (total) {
      arcs<<<blocks, 128>>>(dn.get(),
                            n,
                            dorder.get(),
                            dtracks.get(),
                            nt,
                            rules,
                            nullptr,
                            offsets.get(),
                            de.get(),
                            true);
      check(cudaGetLastError());
    }
    check(cudaMemcpy(nodes,
                     dn.get(),
                     size_t(n) * sizeof(DeviceNode),
                     cudaMemcpyDeviceToHost));
    if (total) {
      auto* result = static_cast<DeviceEdge*>(
          std::malloc(size_t(total) * sizeof(DeviceEdge)));
      if (!result) {
        throw std::bad_alloc();
      }
      const cudaError_t status = cudaMemcpy(result,
                                            de.get(),
                                            size_t(total) * sizeof(DeviceEdge),
                                            cudaMemcpyDeviceToHost);
      if (status != cudaSuccess) {
        std::free(result);
        check(status);
      }
      *edges = result;
    }
    *edge_count = static_cast<size_t>(total);
    return 0;
  } catch (const std::exception& e) {
    if (error_size) {
      std::snprintf(error, error_size, "%s", e.what());
    }
    return 1;
  }
}

extern "C" void delgpuFreeEdges(delgpu::DeviceEdge* edges)
{
  std::free(edges);
}
