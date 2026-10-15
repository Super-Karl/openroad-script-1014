// SPDX-License-Identifier: BSD-3-Clause
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
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

__global__ void validateWarmStart(const OptNode* nodes,
                                  int n,
                                  const double* x,
                                  int* invalid)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n && (!isfinite(x[i]) || x[i] < nodes[i].low || x[i] > nodes[i].up)) {
    atomicExch(invalid, 1);
  }
}
__global__ void moveKernel(const OptNode* nodes,
                           int n,
                           const double* prev,
                           double* trial,
                           const double* direction,
                           const Aggregate* old,
                           double max_step,
                           double learning_rate,
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
  // Scale after normalization so rates >1 actually increase movement.
  const double step = ldexp(scale * learning_rate, -backtrack);
  const double cap = max_step * learning_rate;
  const double x
      = clamp(prev[i] + clamp(step * (direction[i] + jitter), -cap, cap),
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

#include "Postprocess.cuh"

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
       int retries,
       double rate)
      : n(static_cast<int>(hn.size())),
        e(static_cast<int>(he.size())),
        width(w),
        tolerance(tol),
        max_step(step),
        jump_offset(jump),
        learning_rate(rate),
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
        || tol <= 0 || retries < 0 || retries > 19 || !std::isfinite(rate)
        || rate <= 0 || !std::isfinite(step * rate) || step * rate <= 0) {
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
    integer_positions.reset();
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
                                                         learning_rate,
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
    stats.gradient_accepted = accepted;
    return stats;
  }
  int n, e;
  double width, tolerance, max_step, jump_offset, learning_rate;
  int max_backtracks, component_count;
  Device<OptNode> nodes;
  Device<OptEdge> edges;
  Device<OptRange> ranges;
  Device<int> incident, component_edges, component_offsets, edge_components,
      active;
  Device<double> positions, previous, candidate, direction, prime;
  std::unique_ptr<Device<int64_t>> integer_positions;
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
                             int retries,
                             double rate)
    : impl_(std::make_unique<Impl>(n,
                                   e,
                                   r,
                                   i,
                                   c,
                                   o,
                                   components,
                                   w,
                                   t,
                                   step,
                                   jump,
                                   retries,
                                   rate))
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
TrimStats TrimOptimizer::step(double lambda, double learning_rate)
{
  if (!std::isfinite(learning_rate) || learning_rate <= 0
      || !std::isfinite(impl_->max_step * learning_rate)
      || impl_->max_step * learning_rate <= 0) {
    throw std::invalid_argument(
        "Invalid GPU learning rate or effective step cap");
  }
  // Validate before changing the persistent rate.
  if (!std::isfinite(lambda) || lambda <= 0) {
    throw std::invalid_argument("Invalid constraint weight");
  }
  impl_->learning_rate = learning_rate;
  return impl_->step(lambda);
}
void TrimOptimizer::setDisplacementDbu(const std::vector<double>& values)
{
  auto& p = *impl_;
  if (values.size() != static_cast<size_t>(p.n)) {
    throw std::invalid_argument("Invalid warm-start position count");
  }
  std::vector<double> normalized(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    if (!std::isfinite(values[i])) {
      throw std::invalid_argument("Nonfinite warm start");
    }
    normalized[i] = values[i] / p.width;
  }
  Device<int> invalid(1);
  check(cudaMemsetAsync(invalid.get(), 0, invalid.bytes(), p.stream));
  check(cudaMemcpyAsync(p.trial,
                        normalized.data(),
                        p.positions.bytes(),
                        cudaMemcpyHostToDevice,
                        p.stream));
  validateWarmStart<<<blocks(p.n), block_size, 0, p.stream>>>(
      p.nodes.get(), p.n, p.trial, invalid.get());
  int bad = 0;
  check(cudaGetLastError());
  check(cudaMemcpyAsync(
      &bad, invalid.get(), sizeof(bad), cudaMemcpyDeviceToHost, p.stream));
  check(cudaStreamSynchronize(p.stream));
  if (bad) {
    throw std::invalid_argument("Warm start outside movement domain");
  }
  std::swap(p.current, p.trial);
  p.integer_positions.reset();
  check(cudaMemcpyAsync(p.previous.get(),
                        p.current,
                        p.positions.bytes(),
                        cudaMemcpyDeviceToDevice,
                        p.stream));
  if (p.e) {
    check(cudaMemsetAsync(p.stalled.get(), 0, p.stalled.bytes(), p.stream));
    check(cudaMemsetAsync(p.repaired.get(), 0, p.repaired.bytes(), p.stream));
  }
  check(cudaStreamSynchronize(p.stream));
}
std::vector<double> TrimOptimizer::displacementDbu()
{
  std::vector<double> result(impl_->n);
  if (impl_->integer_positions) {
    std::vector<int64_t> exact(impl_->n);
    check(cudaMemcpyAsync(exact.data(),
                          impl_->integer_positions->get(),
                          exact.size() * sizeof(int64_t),
                          cudaMemcpyDeviceToHost,
                          impl_->stream));
    check(cudaStreamSynchronize(impl_->stream));
    std::copy(exact.begin(), exact.end(), result.begin());
    return result;
  }
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
PostprocessResult TrimOptimizer::postprocess(const PostprocessGraph& graph,
                                             const PostprocessOptions& options)
{
  auto& p = *impl_;
  if (graph.nodes.size() != static_cast<size_t>(p.n)
      || graph.initial2.size() != static_cast<size_t>(p.e)
      || graph.ranges.size() * sizeof(OptRange) != p.ranges.bytes()
      || graph.color_nodes.size() != graph.nodes.size()
      || graph.color_offsets.empty() || graph.color_offsets.front() != 0
      || graph.color_offsets.back() != p.n || graph.tolerance2 <= 0
      || options.max_sweeps < 0 || options.pair_rounds < 0
      || options.retract_sweeps < 0 || options.propagation_rounds < 0) {
    throw std::invalid_argument("Invalid exact postprocess graph or budget");
  }
  using Clock = std::chrono::steady_clock;
  const auto begin = Clock::now();
  Device<PostNode> nodes(graph.nodes.size());
  Device<PostRange> ranges(graph.ranges.size());
  Device<int64_t> initial(graph.initial2.size());
  Device<int> order(graph.color_nodes.size()), flags(p.n), next_flags(p.n),
      moves(1);
  Device<post::Pair> candidates(p.e);
  Device<PostprocessStats> work(p.n + p.e), summary(1);
  nodes.upload(graph.nodes);
  ranges.upload(graph.ranges);
  initial.upload(graph.initial2);
  order.upload(graph.color_nodes);
  p.integer_positions = std::make_unique<Device<int64_t>>(p.n);
  post::View v{nodes.get(),
               p.nodes.get(),
               p.edges.get(),
               ranges.get(),
               initial.get(),
               p.incident.get(),
               p.integer_positions->get(),
               graph.tolerance2};
  size_t temp_size = 0;
  check(cub::DeviceReduce::Reduce(nullptr,
                                  temp_size,
                                  work.get(),
                                  summary.get(),
                                  p.n + p.e,
                                  post::Sum{},
                                  PostprocessStats{},
                                  p.stream));
  Device<unsigned char> temp(temp_size);
  const auto stats = [&]() {
    post::statistics<<<blocks(p.n + p.e), block_size, 0, p.stream>>>(
        v, p.n, p.e, work.get());
    check(cudaGetLastError());
    check(cub::DeviceReduce::Reduce(temp.get(),
                                    temp_size,
                                    work.get(),
                                    summary.get(),
                                    p.n + p.e,
                                    post::Sum{},
                                    PostprocessStats{},
                                    p.stream));
    PostprocessStats s;
    check(cudaMemcpyAsync(
        &s, summary.get(), sizeof(s), cudaMemcpyDeviceToHost, p.stream));
    check(cudaStreamSynchronize(p.stream));
    return s;
  };
  const auto moved = [&]() {
    int count = 0;
    check(cudaGetLastError());
    check(cudaMemcpyAsync(
        &count, moves.get(), sizeof(count), cudaMemcpyDeviceToHost, p.stream));
    check(cudaStreamSynchronize(p.stream));
    return count;
  };
  const auto milliseconds = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  PostprocessResult result;
  post::quantize<<<blocks(p.n), block_size, 0, p.stream>>>(
      nodes.get(), p.n, p.current, p.width, v.x);
  result.quantized = stats();
  const auto quantized = Clock::now();
  result.quantize_ms = milliseconds(begin, quantized);
  int* active = flags.get();
  int* next = next_flags.get();
  check(cudaMemsetAsync(active, 0, flags.bytes(), p.stream));
  if (p.e) {
    post::activate<<<blocks(p.e), block_size, 0, p.stream>>>(v, p.e, active);
  }
  const int colors = static_cast<int>(graph.color_offsets.size()) - 1;
  for (int sweep = 0; sweep < options.max_sweeps; ++sweep) {
    check(cudaMemsetAsync(next, 0, flags.bytes(), p.stream));
    check(cudaMemsetAsync(moves.get(), 0, moves.bytes(), p.stream));
    for (int c = 0; c < colors; ++c) {
      const int l = graph.color_offsets[c], u = graph.color_offsets[c + 1];
      if (l < u) {
        post::singles<<<blocks(u - l), block_size, 0, p.stream>>>(
            v, order.get(), l, u, active, next, moves.get());
      }
    }
    const int singles = moved();
    ++result.sweeps;
    result.single_moves += singles;
    if (singles) {
      std::swap(active, next);
      continue;
    }
    if (!p.e || result.pair_rounds >= options.pair_rounds) {
      break;
    }
    post::pairCandidates<<<blocks(p.e), block_size, 0, p.stream>>>(
        v, p.e, candidates.get());
    post::commitPairs<<<p.component_count, 128, 0, p.stream>>>(
        v,
        candidates.get(),
        p.component_edges.get(),
        p.component_offsets.get(),
        next,
        moves.get());
    const int pairs = moved();
    ++result.pair_rounds;
    result.pair_moves += pairs;
    if (!pairs) {
      break;
    }
    std::swap(active, next);
  }
  result.repaired = stats();
  const auto repaired = Clock::now();
  result.repair_ms = milliseconds(quantized, repaired);
  std::unique_ptr<Device<post::Slack>> slack;
  std::unique_ptr<Device<int64_t>> caps, next_caps;
  std::unique_ptr<Device<int>> invalid, parents, labels, heads, links;
  std::unique_ptr<Device<post::GroupMove>> group_candidates, group_selected;
  if (options.cooperative_retract && options.retract_sweeps > 0) {
    slack = std::make_unique<Device<post::Slack>>(p.e);
    caps = std::make_unique<Device<int64_t>>(p.n);
    next_caps = std::make_unique<Device<int64_t>>(p.n);
    invalid = std::make_unique<Device<int>>(p.component_count);
    parents = std::make_unique<Device<int>>(p.n);
    labels = std::make_unique<Device<int>>(p.n);
    heads = std::make_unique<Device<int>>(p.n);
    links = std::make_unique<Device<int>>(p.n);
    group_candidates = std::make_unique<Device<post::GroupMove>>(p.n);
    group_selected
        = std::make_unique<Device<post::GroupMove>>(p.component_count);
  }
  for (int sweep = 0; sweep < options.retract_sweeps; ++sweep) {
    check(cudaMemsetAsync(moves.get(), 0, moves.bytes(), p.stream));
    for (int c = 0; c < colors; ++c) {
      const int l = graph.color_offsets[c], u = graph.color_offsets[c + 1];
      if (l < u) {
        post::retract<<<blocks(u - l), block_size, 0, p.stream>>>(
            v, order.get(), l, u, moves.get());
      }
    }
    const int single_count = moved();
    int collective_count = 0;
    if (options.cooperative_retract && !single_count) {
      if (p.e) {
        post::retractionSlack<<<blocks(p.e), block_size, 0, p.stream>>>(
            v, p.e, slack->get());
      }
      post::initialRetractionCaps<<<blocks(p.n), block_size, 0, p.stream>>>(
          v, p.n, slack->get(), caps->get());
      int64_t* current_caps = caps->get();
      int64_t* trial_caps = next_caps->get();
      // Poll in batches to amortize scalar copies. All propagation reads a
      // frozen coordinate snapshot, so no atomics on positions are needed.
      for (int round = 0; round < options.propagation_rounds;) {
        check(cudaMemsetAsync(moves.get(), 0, moves.bytes(), p.stream));
        const int end = std::min(options.propagation_rounds, round + 8);
        for (; round < end; ++round) {
          post::
              propagateRetractionCaps<<<blocks(p.n), block_size, 0, p.stream>>>(
                  v, p.n, slack->get(), current_caps, trial_caps, moves.get());
          std::swap(current_caps, trial_caps);
          ++result.propagation_rounds;
        }
        if (!moved()) {
          break;
        }
      }
      if (p.e) {
        check(cudaMemsetAsync(invalid->get(), 0, invalid->bytes(), p.stream));
        post::verifyRetraction<<<blocks(p.e), block_size, 0, p.stream>>>(
            v, p.e, current_caps, p.edge_components.get(), invalid->get());
      }
      check(cudaMemsetAsync(moves.get(), 0, moves.bytes(), p.stream));
      post::commitRetraction<<<blocks(p.n), block_size, 0, p.stream>>>(
          v,
          p.n,
          current_caps,
          p.edge_components.get(),
          invalid->get(),
          moves.get());
      collective_count = moved();
      result.cooperative_moves += collective_count;
      if (p.e) {
        post::retractionSlack<<<blocks(p.e), block_size, 0, p.stream>>>(
            v, p.e, slack->get());
        post::initializeGroups<<<blocks(p.n), block_size, 0, p.stream>>>(
            p.n, parents->get(), heads->get(), group_candidates->get());
        post::unionGroups<<<blocks(p.e), block_size, 0, p.stream>>>(
            v, p.e, slack->get(), parents->get());
        post::listGroups<<<blocks(p.n), block_size, 0, p.stream>>>(
            p.n, parents->get(), labels->get(), heads->get(), links->get());
        post::groupCandidates<<<blocks(p.n), block_size, 0, p.stream>>>(
            v,
            p.n,
            slack->get(),
            labels->get(),
            heads->get(),
            links->get(),
            group_candidates->get());
        post::selectGroups<<<p.component_count, 128, 0, p.stream>>>(
            p.edges.get(),
            p.component_edges.get(),
            p.component_offsets.get(),
            labels->get(),
            group_candidates->get(),
            group_selected->get());
        check(cudaMemsetAsync(invalid->get(), 0, invalid->bytes(), p.stream));
        post::verifyGroups<<<blocks(p.e), block_size, 0, p.stream>>>(
            v,
            p.e,
            labels->get(),
            p.edge_components.get(),
            group_selected->get(),
            invalid->get());
        check(cudaMemsetAsync(moves.get(), 0, moves.bytes(), p.stream));
        post::commitGroups<<<blocks(p.n), block_size, 0, p.stream>>>(
            v,
            p.n,
            labels->get(),
            p.edge_components.get(),
            group_selected->get(),
            invalid->get(),
            moves.get());
        const int groups = moved();
        result.group_moves += groups;
        collective_count += groups;
      }
    }
    ++result.retract_sweeps;
    result.retract_moves += single_count + collective_count;
    if (!single_count && !collective_count) {
      break;
    }
  }
  result.retracted = stats();
  const auto objective = [](const PostprocessStats& s) {
    return post::Score{s.remaining[0] + s.remaining[1] + s.remaining[2],
                       s.violations[0] + s.violations[1] + s.violations[2],
                       s.gap2,
                       s.extent};
  };
  if (post::better(objective(result.quantized), objective(result.repaired))
      || post::better(objective(result.repaired),
                      objective(result.retracted))) {
    throw std::runtime_error("GPU postprocess objective increased");
  }
  for (int type = 0; type < 3; ++type) {
    if (result.retracted.violations[type] > result.repaired.violations[type]
        || result.retracted.remaining[type] > result.repaired.remaining[type]) {
      throw std::runtime_error("GPU retraction increased violations");
    }
  }
  if (result.retracted.gap2 > result.repaired.gap2
      || result.retracted.max_gap2 > result.repaired.max_gap2
      || result.retracted.extent > result.repaired.extent) {
    throw std::runtime_error("GPU retraction worsened geometry");
  }
  post::normalized<<<blocks(p.n), block_size, 0, p.stream>>>(
      v.x, p.current, p.n, p.width);
  check(cudaGetLastError());
  check(cudaStreamSynchronize(p.stream));
  const auto end = Clock::now();
  result.retract_ms = milliseconds(repaired, end);
  result.total_ms = milliseconds(begin, end);
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
         + p.summary.bytes() + p.old.bytes() + p.temp_size
         + (p.integer_positions ? p.integer_positions->bytes() : 0);
}
}  // namespace delgpu
