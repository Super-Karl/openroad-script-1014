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
      a.remaining[e.type] = gap + numerical_dbu >= tolerance;
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
                            double width)
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
      = residual(e, ranges, edgeDistance(e, x)) * width > numerical_dbu
        && residual(e, ranges, edgeDistance(e, prev)) * width > numerical_dbu;
  stalled[i] = moved || !violated ? 0 : (stalled[i] < 3 ? stalled[i] + 1 : 3);
}
// A matching owns each endpoint once. Colors execute in stream order.
// Project stalled pairs to their nearest feasible legal interval; unlike the
// CPU barrier jump, this can repair A arcs and tiny residuals directly.
__global__ void repairKernel(const OptNode* nodes,
                             const OptEdge* edges,
                             const OptRange* ranges,
                             const int* colored,
                             int count,
                             double* x,
                             const int* stalled,
                             int* touched,
                             int* repaired,
                             double width,
                             double jump_offset)
{
  const int k = blockIdx.x * blockDim.x + threadIdx.x;
  if (k >= count) {
    return;
  }
  const int id = colored[k];
  if (stalled[id] < 3) {
    return;
  }
  const auto e = edges[id];
  const double d = edgeDistance(e, x);
  if (residual(e, ranges, d) * width <= numerical_dbu) {
    return;
  }
  const double old_s = x[e.src], old_t = x[e.dst];
  const auto s = nodes[e.src], t = nodes[e.dst];
  double best_s = old_s, best_t = old_t, best_cost = INFINITY;
  for (int j = e.range_begin; j < e.range_end; ++j) {
    const auto r = ranges[j];
    double target = d;
    if (r.has_low) {
      target = fmax(target, r.low);
    }
    if (r.has_high) {
      target = fmin(target, r.high);
    }
    const double change = target - d, sign = change >= 0 ? 1 : -1,
                 total = fabs(change);
    const double cs = sign > 0 ? old_s - s.low : s.up - old_s;
    const double ct = sign > 0 ? t.up - old_t : old_t - t.low;
    const double low = fmax(0.0, total - ct), high = fmin(total, cs);
    const double eps = 1e-12 * fmax(1.0, total);
    if (low > high + eps) {
      continue;
    }
    const double qs = 1 + s.end - s.begin, qt = 1 + t.end - t.begin;
    const double share = clamp(total * qt / (qs + qt), fmin(low, high), high);
    const double ns = clamp(old_s - sign * share, s.low, s.up);
    const double nd = clamp(old_t + sign * (total - share), t.low, t.up);
    const double after = e.initial + nd - ns;
    if (!isfinite(ns) || !isfinite(nd)
        || residual(e, ranges, after) * width > numerical_dbu) {
      continue;
    }
    const double cost
        = qs * (ns - old_s) * (ns - old_s) + qt * (nd - old_t) * (nd - old_t);
    if (cost < best_cost) {
      best_cost = cost;
      best_s = ns;
      best_t = nd;
    }
  }
  // If no legal pair position is reachable, use the requested offset to
  // cross a B/C potential peak. Domains remain hard bounds. Best-so-far
  // quality retention protects against a worse final conflict count.
  if (!isfinite(best_cost)
      && ((e.type == 1 && d > 0 && d < e.threshold)
          || (e.type == 2 && d != 0 && fabs(d) < e.threshold))) {
    const double midpoint
        = e.type == 1 ? e.threshold / 2 : copysign(e.threshold / 2, d);
    const double first_side = d <= midpoint ? 1 : -1;
    const int attempts = d == midpoint ? 2 : 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
      const double side = attempt == 0 ? first_side : -first_side;
      const double target = midpoint + side * jump_offset;
      const double change = target - d;
      const double sign = change > 0 ? 1 : -1;
      const double total = fabs(change);
      const double cs = sign > 0 ? old_s - s.low : s.up - old_s;
      const double ct = sign > 0 ? t.up - old_t : old_t - t.low;
      const double low = fmax(0.0, total - ct), high = fmin(total, cs);
      const double eps = 1e-12 * fmax(1.0, fmax(total, fabs(target)));
      if (low > high + eps) {
        continue;
      }
      const double qs = 1 + s.end - s.begin, qt = 1 + t.end - t.begin;
      const double share = clamp(total * qt / (qs + qt), fmin(low, high), high);
      const double ns = clamp(old_s - sign * share, s.low, s.up);
      const double nd = clamp(old_t + sign * (total - share), t.low, t.up);
      const double after = e.initial + nd - ns;
      if (!isfinite(ns) || !isfinite(nd) || (after - midpoint) * side <= 0
          || fabs(after - target) > eps) {
        continue;
      }
      best_s = ns;
      best_t = nd;
      best_cost = 0;
      break;
    }
  }
  if (isfinite(best_cost)) {
    x[e.src] = best_s;
    x[e.dst] = best_t;
    if (best_s != old_s) {
      touched[e.src] = 1;
    }
    if (best_t != old_t) {
      touched[e.dst] = 1;
    }
    repaired[id] = 1;
  }
}
__global__ void clearStalls(const OptEdge* edges,
                            int ne,
                            const int* touched,
                            int* stalled)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= ne) {
    return;
  }
  const auto e = edges[i];
  if (touched[e.src] || touched[e.dst]) {
    stalled[i] = 0;
  }
}
unsigned long long violationCount(const Aggregate& a)
{
  return a.violations[0] + a.violations[1] + a.violations[2];
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
       double w,
       double tol,
       double step,
       double jump)
      : n(static_cast<int>(hn.size())),
        e(static_cast<int>(he.size())),
        width(w),
        tolerance(tol),
        max_step(step),
        jump_offset(jump),
        nodes(hn.size()),
        edges(he.size()),
        ranges(hr.size()),
        incident(hi.size()),
        colored(hc.size()),
        positions(hn.size()),
        previous(hn.size()),
        candidate(hn.size()),
        best(hn.size()),
        direction(hn.size()),
        prime(he.size()),
        stalled(he.size()),
        touched(hn.size()),
        repaired(he.size()),
        work(hn.size() + he.size()),
        summary(1),
        old(1),
        color_offsets(offsets)
  {
    if (n <= 0 || width <= 0 || !std::isfinite(width) || !std::isfinite(step)
        || step <= 0 || !std::isfinite(jump) || jump <= 0) {
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
    colored.upload(hc);
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
      best_stats = report(10);
      check(cudaMemcpyAsync(best.get(),
                            current,
                            positions.bytes(),
                            cudaMemcpyDeviceToDevice,
                            stream));
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
    ++round;
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
      // Eight backtracks bound synchronization overhead; best-so-far tracks
      // quality.
      for (int backtrack = 0; backtrack < 8; ++backtrack) {
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
      stallKernel<<<blocks(e), block_size, 0, stream>>>(edges.get(),
                                                        e,
                                                        ranges.get(),
                                                        previous.get(),
                                                        current,
                                                        stalled.get(),
                                                        width);
      if (round % 3 == 0) {
        check(cudaMemsetAsync(touched.get(), 0, touched.bytes(), stream));
        for (size_t c = 0; c + 1 < color_offsets.size(); ++c) {
          const int count = color_offsets[c + 1] - color_offsets[c];
          if (count) {
            repairKernel<<<blocks(count), block_size, 0, stream>>>(
                nodes.get(),
                edges.get(),
                ranges.get(),
                colored.get() + color_offsets[c],
                count,
                current,
                stalled.get(),
                touched.get(),
                repaired.get(),
                width,
                jump_offset);
          }
        }
        clearStalls<<<blocks(e), block_size, 0, stream>>>(
            edges.get(), e, touched.get(), stalled.get());
      }
    }
    const Aggregate after = report(lambda);
    const auto count = violationCount(after),
               best_count = violationCount(best_stats);
    if (count < best_count
        || (count == best_count
            && (after.gap < best_stats.gap - 1e-9
                || (std::abs(after.gap - best_stats.gap) <= 1e-9
                    && after.extent < best_stats.extent)))) {
      best_stats = after;
      check(cudaMemcpyAsync(best.get(),
                            current,
                            positions.bytes(),
                            cudaMemcpyDeviceToDevice,
                            stream));
    }
    auto stats = unpack(after);
    stats.candidates = candidates;
    stats.accepted = accepted || stats.repaired;
    return stats;
  }
  int n, e, round = 0;
  double width, tolerance, max_step, jump_offset;
  Device<OptNode> nodes;
  Device<OptEdge> edges;
  Device<OptRange> ranges;
  Device<int> incident, colored;
  Device<double> positions, previous, candidate, best, direction, prime;
  Device<int> stalled, touched, repaired;
  Device<Aggregate> work, summary, old;
  std::vector<int> color_offsets;
  std::unique_ptr<Device<unsigned char>> temp;
  size_t temp_size = 0;
  cudaStream_t stream = nullptr;
  Aggregate* host = nullptr;
  double* current = nullptr;
  double* trial = nullptr;
  Aggregate best_stats;
};

TrimOptimizer::TrimOptimizer(const std::vector<OptNode>& n,
                             const std::vector<OptEdge>& e,
                             const std::vector<OptRange>& r,
                             const std::vector<int>& i,
                             const std::vector<int>& c,
                             const std::vector<int>& o,
                             double w,
                             double t,
                             double step,
                             double jump)
    : impl_(std::make_unique<Impl>(n, e, r, i, c, o, w, t, step, jump))
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
TrimStats TrimOptimizer::restoreBest(double lambda)
{
  if (!std::isfinite(lambda) || lambda <= 0) {
    throw std::invalid_argument("Invalid constraint weight");
  }
  check(cudaMemcpyAsync(impl_->current,
                        impl_->best.get(),
                        impl_->positions.bytes(),
                        cudaMemcpyDeviceToDevice,
                        impl_->stream));
  if (impl_->e) {
    check(cudaMemsetAsync(
        impl_->repaired.get(), 0, impl_->repaired.bytes(), impl_->stream));
    check(cudaMemsetAsync(
        impl_->stalled.get(), 0, impl_->stalled.bytes(), impl_->stream));
  }
  check(cudaMemcpyAsync(impl_->previous.get(),
                        impl_->current,
                        impl_->positions.bytes(),
                        cudaMemcpyDeviceToDevice,
                        impl_->stream));
  return unpack(impl_->report(lambda));
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
         + p.incident.bytes() + p.colored.bytes() + p.positions.bytes()
         + p.previous.bytes() + p.candidate.bytes() + p.best.bytes()
         + p.direction.bytes() + p.prime.bytes() + p.stalled.bytes()
         + p.touched.bytes() + p.repaired.bytes() + p.work.bytes()
         + p.summary.bytes() + p.old.bytes() + p.temp_size;
}
}  // namespace delgpu
