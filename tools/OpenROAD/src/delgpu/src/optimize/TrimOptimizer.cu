// SPDX-License-Identifier: BSD-3-Clause
#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cub/device/device_reduce.cuh>
#include <limits>
#include <stdexcept>
#include <string>

#include "delgpu/TrimOptimizer.h"

namespace delgpu {
namespace {
constexpr int block_size = 256;
constexpr double numerical_dbu = 1e-6;
void check(cudaError_t status)
{
  if (status != cudaSuccess) {
    throw std::runtime_error(cudaGetErrorString(status));
  }
}
int blocks(int n)
{
  return n ? (n - 1) / block_size + 1 : 0;
}
template <typename T>
class Device
{
 public:
  explicit Device(size_t count) : size_(count)
  {
    if (count) {
      check(cudaMalloc(reinterpret_cast<void**>(&data_), count * sizeof(T)));
    }
  }
  ~Device()
  {
    if (data_) {
      cudaFree(data_);
    }
  }
  Device(const Device&) = delete;
  T* get() const { return data_; }
  size_t bytes() const { return size_ * sizeof(T); }
  void upload(const std::vector<T>& values)
  {
    if (!values.empty()) {
      check(cudaMemcpy(data_, values.data(), bytes(), cudaMemcpyHostToDevice));
    }
  }

 private:
  T* data_ = nullptr;
  size_t size_;
};
struct Aggregate
{
  double energy = 0, rule = 0, extent = 0, gap = 0, change = 0, direction = 0;
  unsigned long long violations[3]{}, remaining[3]{};
  unsigned long long moved = 0, repaired = 0;
};
struct Combine
{
  __host__ __device__ Aggregate operator()(const Aggregate& a,
                                           const Aggregate& b) const
  {
    Aggregate r;
    r.energy = a.energy + b.energy;
    r.rule = a.rule + b.rule;
    r.extent = a.extent + b.extent;
    r.gap = a.gap > b.gap ? a.gap : b.gap;
    r.change = a.change > b.change ? a.change : b.change;
    r.direction = a.direction > b.direction ? a.direction : b.direction;
    r.moved = a.moved + b.moved;
    r.repaired = a.repaired + b.repaired;
    for (int i = 0; i < 3; ++i) {
      r.violations[i] = a.violations[i] + b.violations[i];
      r.remaining[i] = a.remaining[i] + b.remaining[i];
    }
    return r;
  }
};
__device__ double clamp(double x, double lo, double hi)
{
  return fmin(hi, fmax(lo, x));
}
__device__ double edgeDistance(const OptEdge& e, const double* x)
{
  return e.initial + x[e.dst] - x[e.src];
}
__device__ double residual(const OptEdge& e, const OptRange* ranges, double d)
{
  double gap = INFINITY;
  for (int i = e.range_begin; i < e.range_end; ++i) {
    const auto r = ranges[i];
    double g = 0;
    if (r.has_low) {
      g = fmax(g, r.low - d);
    }
    if (r.has_high) {
      g = fmax(g, d - r.high);
    }
    gap = fmin(gap, g);
  }
  return gap;
}
__device__ void potential(const OptEdge& e,
                          double d,
                          double& value,
                          double& derivative)
{
  if (e.type == 0 || (e.type == 1 && d < 0)) {
    const double g = fmin(0.0, d - (e.type == 0 ? e.threshold : 0));
    value = g * g;
    derivative = 2 * g;
    return;
  }
  const double lo = e.type == 2 && d < 0 ? -e.threshold : 0;
  const double hi = e.type == 2 && d < 0 ? 0 : e.threshold;
  if (d <= lo || d >= hi) {
    value = 0;
    derivative = 0;
    return;
  }
  const double t = (d - lo) / (hi - lo);
  value = 16 * t * t * (1 - t) * (1 - t);
  derivative = 32 * t * (1 - t) * (1 - 2 * t) / (hi - lo);
}

template <bool derivatives, bool report>
__global__ void potentialKernel(const OptEdge* edges,
                                int ne,
                                const OptRange* ranges,
                                const double* x,
                                double lambda,
                                double width,
                                double tolerance,
                                double* prime,
                                Aggregate* work,
                                const int* repaired,
                                int nn)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= ne) {
    return;
  }
  const auto e = edges[i];
  const double d = edgeDistance(e, x);
  double phi, dp;
  potential(e, d, phi, dp);
  Aggregate a;
  a.energy = lambda * phi;
  a.rule = phi;
  if (derivatives) {
    prime[i] = dp;
  }
  if (report) {
    const double gap = residual(e, ranges, d) * width;
    if (gap > numerical_dbu) {
      a.violations[e.type] = 1;
      a.remaining[e.type] = gap >= tolerance;
      a.gap = gap;
    }
    a.repaired = repaired[i];
  }
  work[nn + i] = a;
}

template <bool gradient>
__global__ void gradientKernel(const OptNode* nodes,
                               int n,
                               const OptEdge* edges,
                               const int* incident,
                               const double* prime,
                               const double* x,
                               const double* prev,
                               double lambda,
                               double width,
                               double* direction,
                               Aggregate* work)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) {
    return;
  }
  const auto node = nodes[i];
  const double pos = x[i], smooth = hypot(pos, 0.01);
  Aggregate a;
  a.energy = 0.01 * (smooth - 0.01);
  a.extent = fabs(pos) * width;
  const double change = fabs(pos - prev[i]);
  a.change = change * width;
  a.moved = change > 1e-12;
  if (gradient) {
    double g = 0.01 * pos / smooth;
    for (int k = node.begin; k < node.end; ++k) {
      const int id = incident[k];
      g += (edges[id].src == i ? -1 : 1) * lambda * prime[id];
    }
    const double p = clamp(
        -g / (1 + node.end - node.begin), node.low - pos, node.up - pos);
    direction[i] = p;
    a.direction = fabs(p);
  }
  work[i] = a;
}

__global__ void moveKernel(const OptNode* nodes,
                           int n,
                           const double* prev,
                           double* trial,
                           const double* direction,
                           const Aggregate* old,
                           double max_step,
                           int attempt,
                           int backtrack,
                           double width,
                           Aggregate* work)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= n) {
    return;
  }
  const double scale
      = old->direction > max_step ? max_step / old->direction : 1;
  const double jitter = attempt ? ((i + attempt) % 2 == 0 ? 0.01 : -0.01) : 0;
  const double step = ldexp(scale, -backtrack);
  const double x = clamp(
      prev[i] + clamp(step * (direction[i] + jitter), -max_step, max_step),
      nodes[i].low,
      nodes[i].up);
  trial[i] = x;
  Aggregate a;
  a.energy = 0.01 * (hypot(x, 0.01) - 0.01);
  a.extent = fabs(x) * width;
  const double change = fabs(x - prev[i]);
  a.moved = change > 1e-12;
  a.change = change * width;
  work[i] = a;
}
__global__ void stallKernel(const OptEdge* edges,
                            int ne,
                            const OptRange* ranges,
                            const double* prev,
                            const double* x,
                            int* stalled,
                            double width,
                            double tolerance,
                            const int* edge_components,
                            int* active)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= ne) {
    return;
  }
  const auto e = edges[i];
  const bool moved
      = llround(width * x[e.src]) != llround(width * prev[e.src])
        || llround(width * x[e.dst]) != llround(width * prev[e.dst]);
  const bool violated
      = residual(e, ranges, edgeDistance(e, x)) * width >= tolerance
        && residual(e, ranges, edgeDistance(e, prev)) * width >= tolerance;
  stalled[i] = moved || !violated ? 0 : (stalled[i] < 3 ? stalled[i] + 1 : 3);
  if (stalled[i] >= 3) {
    atomicExch(active + edge_components[i], 1);
  }
}
// The CPU barrier jump, including joint feasibility and degree-weighted
// endpoint movement. A and negative-distance B have no barrier to cross.
__device__ bool jumpAcrossBarrier(const OptNode* nodes,
                                  const OptEdge& e,
                                  double* x,
                                  double jump_offset)
{
  const double d = edgeDistance(e, x);
  double midpoint;
  if (e.type == 1 && d > 0 && d < e.threshold) {
    midpoint = e.threshold / 2;
  } else if (e.type == 2 && d != 0 && fabs(d) < e.threshold) {
    midpoint = copysign(e.threshold / 2, d);
  } else {
    return false;
  }
  const double first_side = d <= midpoint ? 1 : -1;
  const int attempts = d == midpoint ? 2 : 1;
  const auto s = nodes[e.src], t = nodes[e.dst];
  const double old_s = x[e.src], old_t = x[e.dst];
  for (int attempt = 0; attempt < attempts; ++attempt) {
    const double side = attempt == 0 ? first_side : -first_side;
    const double target = midpoint + side * jump_offset;
    const double change = target - d, sign = change > 0 ? 1 : -1;
    const double total = fabs(change);
    const double cs = sign > 0 ? old_s - s.low : s.up - old_s;
    const double ct = sign > 0 ? t.up - old_t : old_t - t.low;
    const double low = fmax(0.0, total - ct), high = fmin(total, cs);
    const double roundoff
        = 16 * std::numeric_limits<double>::epsilon()
          * fmax(
              1.0,
              fmax(total,
                   fmax(fabs(d),
                        fmax(fabs(target), fmax(fabs(old_s), fabs(old_t))))));
    if (low > high + roundoff) {
      continue;
    }
    const double qs = 1 + s.end - s.begin, qt = 1 + t.end - t.begin;
    const double preferred = total * (qt / (qs + qt));
    const double share = low > high ? high : clamp(preferred, low, high);
    const double ns = clamp(old_s - sign * share, s.low, s.up);
    const double nd = clamp(old_t + sign * (total - share), t.low, t.up);
    const double after = e.initial + nd - ns;
    if (!isfinite(ns) || !isfinite(nd) || (after - midpoint) * side <= 0
        || fabs(after - target) > roundoff) {
      continue;
    }
    x[e.src] = ns;
    x[e.dst] = nd;
    return true;
  }
  return false;
}
// One thread owns a component. Components have disjoint nodes and incident
// edges, so the canonical scan and immediate resets need no atomics or locks.
__global__ void repairKernel(const OptNode* nodes,
                             const OptEdge* edges,
                             const int* incident,
                             const int* component_edges,
                             const int* component_offsets,
                             int component_count,
                             const int* active,
                             double* x,
                             int* stalled,
                             int* repaired,
                             double jump_offset)
{
  const int component = blockIdx.x * blockDim.x + threadIdx.x;
  if (component >= component_count || !active[component]) {
    return;
  }
  for (int k = component_offsets[component];
       k < component_offsets[component + 1];
       ++k) {
    const int id = component_edges[k];
    if (stalled[id] < 3) {
      continue;
    }
    const auto e = edges[id];
    const double old_s = x[e.src], old_t = x[e.dst];
    if (!jumpAcrossBarrier(nodes, e, x, jump_offset)) {
      continue;
    }
    repaired[id] = 1;
    if (x[e.src] != old_s) {
      for (int j = nodes[e.src].begin; j < nodes[e.src].end; ++j) {
        stalled[incident[j]] = 0;
      }
    }
    if (x[e.dst] != old_t) {
      for (int j = nodes[e.dst].begin; j < nodes[e.dst].end; ++j) {
        stalled[incident[j]] = 0;
      }
    }
  }
}
TrimStats unpack(const Aggregate& a)
{
  TrimStats s;
  s.energy = a.energy;
  s.rule_energy = a.rule;
  s.extent_dbu = a.extent;
  s.max_violation_dbu = a.gap;
  s.max_step_dbu = a.change;
  s.moved = a.moved;
  s.repaired = a.repaired;
  for (int i = 0; i < 3; ++i) {
    s.violations[i] = a.violations[i];
    s.remaining[i] = a.remaining[i];
  }
  if (!std::isfinite(s.energy) || !std::isfinite(s.max_violation_dbu)) {
    throw std::runtime_error("Non-finite CUDA optimization state");
  }
  return s;
}
}  // namespace

class TrimOptimizer::Impl
{
 public:
  Impl(const std::vector<OptNode>& hn,
       const std::vector<OptEdge>& he,
       const std::vector<OptRange>& hr,
       const std::vector<int>& hi,
       const std::vector<int>& hc,
       const std::vector<int>& offsets,
       const std::vector<int>& components,
       double w,
       double tol,
       double step,
       double jump,
       int retries)
      : n(static_cast<int>(hn.size())),
        e(static_cast<int>(he.size())),
        width(w),
        tolerance(tol),
        max_step(step),
        jump_offset(jump),
        max_backtracks(retries),
        component_count(static_cast<int>(offsets.size()) - 1),
        nodes(hn.size()),
        edges(he.size()),
        ranges(hr.size()),
        incident(hi.size()),
        component_edges(hc.size()),
        component_offsets(offsets.size()),
        edge_components(components.size()),
        active(component_count),
        positions(hn.size()),
        previous(hn.size()),
        candidate(hn.size()),
        direction(hn.size()),
        prime(he.size()),
        stalled(he.size()),
        repaired(he.size()),
        work(hn.size() + he.size()),
        summary(1),
        old(1)
  {
    if (n <= 0 || width <= 0 || !std::isfinite(width) || !std::isfinite(step)
        || step <= 0 || !std::isfinite(jump) || jump <= 0 || !std::isfinite(tol)
        || tol <= 0 || retries < 0 || retries > 19) {
      throw std::invalid_argument("Invalid GPU optimizer parameters");
    }
    for (const auto& node : hn) {
      if (!std::isfinite(node.low) || !std::isfinite(node.up) || node.low > 0
          || node.up < 0 || node.begin < 0 || node.end < node.begin) {
        throw std::invalid_argument("Initial coordinate outside GPU domain");
      }
    }
    nodes.upload(hn);
    edges.upload(he);
    ranges.upload(hr);
    incident.upload(hi);
    component_edges.upload(hc);
    component_offsets.upload(offsets);
    edge_components.upload(components);
    check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
    try {
      check(cudaMallocHost(reinterpret_cast<void**>(&host), sizeof(Aggregate)));
      check(cudaMemsetAsync(positions.get(), 0, positions.bytes(), stream));
      check(cudaMemsetAsync(previous.get(), 0, previous.bytes(), stream));
      if (e) {
        check(cudaMemsetAsync(stalled.get(), 0, stalled.bytes(), stream));
        check(cudaMemsetAsync(repaired.get(), 0, repaired.bytes(), stream));
      }
      current = positions.get();
      trial = candidate.get();
      check(cub::DeviceReduce::Reduce(nullptr,
                                      temp_size,
                                      work.get(),
                                      summary.get(),
                                      n + e,
                                      Combine{},
                                      Aggregate{},
                                      stream));
      temp = std::make_unique<Device<unsigned char>>(temp_size);
      check(cudaStreamSynchronize(stream));
    } catch (...) {
      if (host) {
        cudaFreeHost(host);
      }
      cudaStreamDestroy(stream);
      throw;
    }
  }
  ~Impl()
  {
    cudaStreamSynchronize(stream);
    cudaFreeHost(host);
    cudaStreamDestroy(stream);
  }
  void reduce(Aggregate* output)
  {
    check(cudaGetLastError());
    check(cub::DeviceReduce::Reduce(temp->get(),
                                    temp_size,
                                    work.get(),
                                    output,
                                    n + e,
                                    Combine{},
                                    Aggregate{},
                                    stream));
  }
  Aggregate download(Aggregate* output)
  {
    check(cudaMemcpyAsync(
        host, output, sizeof(Aggregate), cudaMemcpyDeviceToHost, stream));
    check(cudaStreamSynchronize(stream));
    unpack(*host);
    return *host;
  }
  Aggregate report(double lambda)
  {
    if (e) {
      potentialKernel<false, true>
          <<<blocks(e), block_size, 0, stream>>>(edges.get(),
                                                 e,
                                                 ranges.get(),
                                                 current,
                                                 lambda,
                                                 width,
                                                 tolerance,
                                                 prime.get(),
                                                 work.get(),
                                                 repaired.get(),
                                                 n);
    }
    gradientKernel<false><<<blocks(n), block_size, 0, stream>>>(nodes.get(),
                                                                n,
                                                                edges.get(),
                                                                incident.get(),
                                                                prime.get(),
                                                                current,
                                                                previous.get(),
                                                                lambda,
                                                                width,
                                                                direction.get(),
                                                                work.get());
    reduce(summary.get());
    return download(summary.get());
  }
  TrimStats step(double lambda)
  {
    check(cudaMemcpyAsync(previous.get(),
                          current,
                          positions.bytes(),
                          cudaMemcpyDeviceToDevice,
                          stream));
    if (e) {
      potentialKernel<true, false>
          <<<blocks(e), block_size, 0, stream>>>(edges.get(),
                                                 e,
                                                 ranges.get(),
                                                 current,
                                                 lambda,
                                                 width,
                                                 tolerance,
                                                 prime.get(),
                                                 work.get(),
                                                 repaired.get(),
                                                 n);
    }
    gradientKernel<true><<<blocks(n), block_size, 0, stream>>>(nodes.get(),
                                                               n,
                                                               edges.get(),
                                                               incident.get(),
                                                               prime.get(),
                                                               current,
                                                               previous.get(),
                                                               lambda,
                                                               width,
                                                               direction.get(),
                                                               work.get());
    reduce(old.get());
    const Aggregate before = download(old.get());
    int candidates = 0;
    bool accepted = false;
    for (int attempt = 0; attempt < 3 && !accepted; ++attempt) {
      // Zero retries evaluates only the initial step for each attempt.
      for (int backtrack = 0; backtrack <= max_backtracks; ++backtrack) {
        moveKernel<<<blocks(n), block_size, 0, stream>>>(nodes.get(),
                                                         n,
                                                         previous.get(),
                                                         trial,
                                                         direction.get(),
                                                         old.get(),
                                                         max_step,
                                                         attempt,
                                                         backtrack,
                                                         width,
                                                         work.get());
        if (e) {
          potentialKernel<false, false>
              <<<blocks(e), block_size, 0, stream>>>(edges.get(),
                                                     e,
                                                     ranges.get(),
                                                     trial,
                                                     lambda,
                                                     width,
                                                     tolerance,
                                                     prime.get(),
                                                     work.get(),
                                                     repaired.get(),
                                                     n);
        }
        reduce(summary.get());
        const Aggregate next = download(summary.get());
        ++candidates;
        if (next.moved == 0) {
          break;
        }
        if (next.energy
            < before.energy - 1e-12 * std::max(1.0, before.energy)) {
          std::swap(current, trial);
          accepted = true;
          break;
        }
      }
      if (before.rule == 0) {
        break;
      }
    }
    if (e) {
      check(cudaMemsetAsync(repaired.get(), 0, repaired.bytes(), stream));
      check(cudaMemsetAsync(active.get(), 0, active.bytes(), stream));
      stallKernel<<<blocks(e), block_size, 0, stream>>>(edges.get(),
                                                        e,
                                                        ranges.get(),
                                                        previous.get(),
                                                        current,
                                                        stalled.get(),
                                                        width,
                                                        tolerance,
                                                        edge_components.get(),
                                                        active.get());
      repairKernel<<<blocks(component_count), block_size, 0, stream>>>(
          nodes.get(),
          edges.get(),
          incident.get(),
          component_edges.get(),
          component_offsets.get(),
          component_count,
          active.get(),
          current,
          stalled.get(),
          repaired.get(),
          jump_offset);
    }
    const Aggregate after = report(lambda);
    auto stats = unpack(after);
    stats.candidates = candidates;
    stats.accepted = accepted || stats.repaired;
    return stats;
  }
  int n, e;
  double width, tolerance, max_step, jump_offset;
  int max_backtracks, component_count;
  Device<OptNode> nodes;
  Device<OptEdge> edges;
  Device<OptRange> ranges;
  Device<int> incident, component_edges, component_offsets, edge_components,
      active;
  Device<double> positions, previous, candidate, direction, prime;
  Device<int> stalled, repaired;
  Device<Aggregate> work, summary, old;
  std::unique_ptr<Device<unsigned char>> temp;
  size_t temp_size = 0;
  cudaStream_t stream = nullptr;
  Aggregate* host = nullptr;
  double* current = nullptr;
  double* trial = nullptr;
};

TrimOptimizer::TrimOptimizer(const std::vector<OptNode>& n,
                             const std::vector<OptEdge>& e,
                             const std::vector<OptRange>& r,
                             const std::vector<int>& i,
                             const std::vector<int>& c,
                             const std::vector<int>& o,
                             const std::vector<int>& components,
                             double w,
                             double t,
                             double step,
                             double jump,
                             int retries)
    : impl_(std::make_unique<
            Impl>(n, e, r, i, c, o, components, w, t, step, jump, retries))
{
}
TrimOptimizer::~TrimOptimizer() = default;
TrimStats TrimOptimizer::evaluate(double lambda)
{
  if (!std::isfinite(lambda) || lambda <= 0) {
    throw std::invalid_argument("Invalid constraint weight");
  }
  return unpack(impl_->report(lambda));
}
TrimStats TrimOptimizer::step(double lambda)
{
  if (!std::isfinite(lambda) || lambda <= 0) {
    throw std::invalid_argument("Invalid constraint weight");
  }
  return impl_->step(lambda);
}
std::vector<double> TrimOptimizer::displacementDbu()
{
  std::vector<double> result(impl_->n);
  check(cudaMemcpyAsync(result.data(),
                        impl_->current,
                        impl_->positions.bytes(),
                        cudaMemcpyDeviceToHost,
                        impl_->stream));
  check(cudaStreamSynchronize(impl_->stream));
  for (auto& v : result) {
    v *= impl_->width;
  }
  return result;
}
size_t TrimOptimizer::deviceBytes() const
{
  const auto& p = *impl_;
  return p.nodes.bytes() + p.edges.bytes() + p.ranges.bytes()
         + p.incident.bytes() + p.component_edges.bytes()
         + p.component_offsets.bytes() + p.edge_components.bytes()
         + p.active.bytes() + p.positions.bytes() + p.previous.bytes()
         + p.candidate.bytes() + p.direction.bytes() + p.prime.bytes()
         + p.stalled.bytes() + p.repaired.bytes() + p.work.bytes()
         + p.summary.bytes() + p.old.bytes() + p.temp_size;
}
}  // namespace delgpu
