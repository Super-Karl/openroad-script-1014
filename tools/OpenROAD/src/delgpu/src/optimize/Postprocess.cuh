// SPDX-License-Identifier: BSD-3-Clause
// Included by TrimOptimizer.cu inside namespace delgpu. Reuses its stream,
// device buffer RAII and CUDA error handling; no OpenDB or GUI on the device.
namespace post {
using I = int64_t;
__host__ __device__ I lo(I a, I b)
{
  return a < b ? a : b;
}
__host__ __device__ I hi(I a, I b)
{
  return a > b ? a : b;
}
__host__ __device__ I absolute(I a)
{
  return a < 0 ? -a : a;
}
__host__ __device__ I bound(I a, I l, I u)
{
  return lo(u, hi(l, a));
}
__host__ __device__ I floorHalf(I a)
{
  return a / 2 - (a < 0 && a % 2);
}
__host__ __device__ I ceilHalf(I a)
{
  return a / 2 + (a > 0 && a % 2);
}
struct Score
{
  I severe = 0, conflicts = 0, gap = 0, extent = 0;
};
__host__ __device__ bool better(const Score& a, const Score& b)
{
  if (a.severe != b.severe) {
    return a.severe < b.severe;
  }
  if (a.conflicts != b.conflicts) {
    return a.conflicts < b.conflicts;
  }
  if (a.gap != b.gap) {
    return a.gap < b.gap;
  }
  return a.extent < b.extent;
}
struct View
{
  const PostNode* nodes;
  const OptNode* csr;
  const OptEdge* edges;
  const PostRange* ranges;
  const I* initial;
  const int* incident;
  I* x;
  I tolerance;
};
__device__ I
distance(const View& v, int id, int a = -1, I xa = 0, int b = -1, I xb = 0)
{
  const auto e = v.edges[id];
  const I s = e.src == a ? xa : (e.src == b ? xb : v.x[e.src]);
  const I t = e.dst == a ? xa : (e.dst == b ? xb : v.x[e.dst]);
  return v.initial[id] + 2 * (t - s);
}
__device__ I gap(const View& v, int id, I d)
{
  I best = INT64_MAX;
  const auto e = v.edges[id];
  for (int j = e.range_begin; j < e.range_end; ++j) {
    const auto r = v.ranges[j];
    I g = 0;
    if (r.has_low) {
      g = hi(g, r.low2 - d);
    }
    if (r.has_high) {
      g = hi(g, d - r.high2);
    }
    best = lo(best, g);
  }
  return best;
}
__device__ void addDelta(Score& s, I before, I after, I tol)
{
  s.severe += (after >= tol) - (before >= tol);
  s.conflicts += (after > 0) - (before > 0);
  s.gap += after - before;
}
__device__ Score evaluate(const View& v, int a, I xa, int b = -1, I xb = 0)
{
  Score s;
  s.extent = absolute(xa) - absolute(v.x[a]);
  if (b >= 0) {
    s.extent += absolute(xb) - absolute(v.x[b]);
  }
  for (int endpoint = 0; endpoint < (b >= 0 ? 2 : 1); ++endpoint) {
    const int node = endpoint ? b : a;
    const auto n = v.csr[node];
    for (int k = n.begin; k < n.end; ++k) {
      const int id = v.incident[k];
      const auto e = v.edges[id];
      // Count every edge in the union once, including duplicate/parallel arcs.
      if (endpoint && (e.src == a || e.dst == a)) {
        continue;
      }
      addDelta(s,
               gap(v, id, distance(v, id)),
               gap(v, id, distance(v, id, a, xa, b, xb)),
               v.tolerance);
    }
  }
  return s;
}
__device__ void markNeighbors(const View& v, int node, int* next)
{
  atomicExch(next + node, 1);
  const auto n = v.csr[node];
  for (int k = n.begin; k < n.end; ++k) {
    const auto e = v.edges[v.incident[k]];
    atomicExch(next + (e.src == node ? e.dst : e.src), 1);
  }
}
__global__ void quantize(const PostNode* nodes,
                         int n,
                         const double* positions,
                         double width,
                         I* x)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    x[i] = bound(llround(positions[i] * width), nodes[i].low, nodes[i].up);
  }
}
__global__ void activate(View v, int ne, int* flags)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < ne && gap(v, i, distance(v, i)) > 0) {
    markNeighbors(v, v.edges[i].src, flags);
    markNeighbors(v, v.edges[i].dst, flags);
  }
}
__device__ void trySingle(const View& v,
                          int node,
                          I candidate,
                          I& best,
                          Score& score)
{
  const auto n = v.nodes[node];
  candidate = bound(candidate, n.low, n.up);
  if (candidate == v.x[node] || candidate == best) {
    return;
  }
  const Score next = evaluate(v, node, candidate);
  if (better(next, score)) {
    score = next;
    best = candidate;
  }
}
__global__ void singles(View v,
                        const int* order,
                        int begin,
                        int end,
                        const int* active,
                        int* next,
                        int* moves)
{
  const int index = begin + blockIdx.x * blockDim.x + threadIdx.x;
  if (index >= end) {
    return;
  }
  const int node = order[index];
  if (!active[node]) {
    return;
  }
  const I old = v.x[node];
  I best = old;
  Score score;
  trySingle(v, node, 0, best, score);
  trySingle(v, node, v.nodes[node].low, best, score);
  trySingle(v, node, v.nodes[node].up, best, score);
  for (I step = 1; step <= 8; step *= 2) {
    trySingle(v, node, old - step, best, score);
    trySingle(v, node, old + step, best, score);
  }
  const auto n = v.csr[node];
  for (int k = n.begin; k < n.end; ++k) {
    const int id = v.incident[k];
    const auto e = v.edges[id];
    const I d = distance(v, id);
    for (int j = e.range_begin; j < e.range_end; ++j) {
      const auto r = v.ranges[j];
      for (int side = 0; side < 2; ++side) {
        if (side ? !r.has_high : !r.has_low) {
          continue;
        }
        const I target = side ? r.high2 : r.low2;
        const I numerator = e.src == node ? d - target : target - d;
        trySingle(v, node, old + floorHalf(numerator), best, score);
        trySingle(v, node, old + ceilHalf(numerator), best, score);
      }
    }
  }
  if (best != old) {
    v.x[node] = best;
    atomicAdd(moves, 1);
    markNeighbors(v, node, next);
  }
}
struct Pair
{
  Score delta;
  I source = 0, target = 0;
  int edge = -1;
};
__device__ bool pairBetter(const Pair& a, const Pair& b)
{
  if (a.edge < 0) {
    return false;
  }
  if (b.edge < 0) {
    return true;
  }
  if (better(a.delta, b.delta)) {
    return true;
  }
  return !better(b.delta, a.delta) && a.edge < b.edge;
}
__device__ void
tryPair(const View& v, int id, I q, I source, I low, I up, Pair& best)
{
  source = bound(source, low, up);
  const auto e = v.edges[id];
  Pair p;
  p.edge = id;
  p.source = source;
  p.target = source + q;
  p.delta = evaluate(v, e.src, p.source, e.dst, p.target);
  if (better(p.delta, Score{}) && pairBetter(p, best)) {
    best = p;
  }
}
__global__ void pairCandidates(View v, int ne, Pair* candidates)
{
  const int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= ne) {
    return;
  }
  Pair best;
  if (gap(v, id, distance(v, id)) > 0) {
    const auto e = v.edges[id];
    const auto ns = v.nodes[e.src], nt = v.nodes[e.dst];
    const I xs = v.x[e.src], xt = v.x[e.dst], oldq = xt - xs;
    const I reach_low = nt.low - ns.up, reach_up = nt.up - ns.low;
    for (int j = e.range_begin; j < e.range_end; ++j) {
      const auto r = v.ranges[j];
      const I lowq = r.has_low ? hi(reach_low, ceilHalf(r.low2 - v.initial[id]))
                               : reach_low;
      const I upq = r.has_high
                        ? lo(reach_up, floorHalf(r.high2 - v.initial[id]))
                        : reach_up;
      if (lowq > upq) {
        continue;
      }
      const I targets[3] = {bound(oldq, lowq, upq), lowq, upq};
      for (I q : targets) {
        const I l = hi(ns.low, nt.low - q), u = lo(ns.up, nt.up - q);
        if (l > u) {
          continue;
        }
        const int ds = 1 + v.csr[e.src].end - v.csr[e.src].begin;
        const int dt = 1 + v.csr[e.dst].end - v.csr[e.dst].begin;
        const I preferred
            = xs - llround(double(q - oldq) * dt / (double(ds) + dt));
        const I choices[7] = {xs, xt - q, preferred, l, u, 0, -q};
        for (I source : choices) {
          tryPair(v, id, q, source, l, u, best);
        }
      }
    }
  }
  candidates[id] = best;
}
// One block per independent component: parallel selection, one safe commit.
__global__ void commitPairs(View v,
                            const Pair* candidates,
                            const int* order,
                            const int* offsets,
                            int* next,
                            int* moves)
{
  __shared__ __align__(16) unsigned char storage[sizeof(Pair) * 128];
  Pair* winners = reinterpret_cast<Pair*>(storage);
  const int c = blockIdx.x, t = threadIdx.x;
  Pair best;
  for (int k = offsets[c] + t; k < offsets[c + 1]; k += blockDim.x) {
    const Pair p = candidates[order[k]];
    if (pairBetter(p, best)) {
      best = p;
    }
  }
  winners[t] = best;
  __syncthreads();
  for (int stride = blockDim.x / 2; stride; stride /= 2) {
    if (t < stride && pairBetter(winners[t + stride], winners[t])) {
      winners[t] = winners[t + stride];
    }
    __syncthreads();
  }
  if (t == 0 && winners[0].edge >= 0) {
    const Pair p = winners[0];
    const auto e = v.edges[p.edge];
    v.x[e.src] = p.source;
    v.x[e.dst] = p.target;
    markNeighbors(v, e.src, next);
    markNeighbors(v, e.dst, next);
    atomicAdd(moves, 1);
  }
}
__global__ void retract(View v,
                        const int* order,
                        int begin,
                        int end,
                        int* moves)
{
  const int index = begin + blockIdx.x * blockDim.x + threadIdx.x;
  if (index >= end) {
    return;
  }
  const int node = order[index];
  const I old = v.x[node];
  if (old == 0) {
    return;
  }
  I low = v.nodes[node].low, up = v.nodes[node].up;
  const auto n = v.csr[node];
  for (int k = n.begin; k < n.end; ++k) {
    const int id = v.incident[k];
    const auto e = v.edges[id];
    const I d = distance(v, id), g = gap(v, id, d);
    for (int j = e.range_begin; j < e.range_end; ++j) {
      const auto r = v.ranges[j];
      // Keep a branch of the expanded legal set containing the current point.
      if ((r.has_low && d < r.low2 - g) || (r.has_high && d > r.high2 + g)) {
        continue;
      }
      if (e.dst == node) {
        const I base = v.initial[id] - 2 * v.x[e.src];
        if (r.has_low) {
          low = hi(low, ceilHalf(r.low2 - g - base));
        }
        if (r.has_high) {
          up = lo(up, floorHalf(r.high2 + g - base));
        }
      } else {
        const I base = v.initial[id] + 2 * v.x[e.dst];
        if (r.has_high) {
          low = hi(low, ceilHalf(base - r.high2 - g));
        }
        if (r.has_low) {
          up = lo(up, floorHalf(base - r.low2 + g));
        }
      }
      break;
    }
  }
  const I candidate = bound(0, low, up);
  if (low > up || absolute(candidate) >= absolute(old)) {
    return;
  }
  // Independent per-edge guard, stronger than the repair objective.
  for (int k = n.begin; k < n.end; ++k) {
    const int id = v.incident[k];
    if (gap(v, id, distance(v, id, node, candidate))
        > gap(v, id, distance(v, id))) {
      return;
    }
  }
  v.x[node] = candidate;
  atomicAdd(moves, 1);
}
struct Sum
{
  __host__ __device__ PostprocessStats
  operator()(const PostprocessStats& a, const PostprocessStats& b) const
  {
    PostprocessStats s;
    s.gap2 = a.gap2 + b.gap2;
    s.max_gap2 = hi(a.max_gap2, b.max_gap2);
    s.extent = a.extent + b.extent;
    for (int i = 0; i < 3; ++i) {
      s.violations[i] = a.violations[i] + b.violations[i];
      s.remaining[i] = a.remaining[i] + b.remaining[i];
    }
    return s;
  }
};
__global__ void statistics(View v, int nn, int ne, PostprocessStats* work)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= nn + ne) {
    return;
  }
  PostprocessStats s;
  if (i < nn) {
    s.extent = absolute(v.x[i]);
  } else {
    const int id = i - nn, type = v.edges[id].type;
    const I g = gap(v, id, distance(v, id));
    s.violations[type] = g > 0;
    s.remaining[type] = g >= v.tolerance;
    s.gap2 = s.max_gap2 = g;
  }
  work[i] = s;
}
__global__ void normalized(const I* x, double* positions, int n, double width)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    positions[i] = x[i] / width;
  }
}
}  // namespace post
