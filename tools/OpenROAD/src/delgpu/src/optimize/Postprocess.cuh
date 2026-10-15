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
// CPU DEL polish fixes the nearest expanded interval and optimizes all node
// positions together. Here we compute a feasible simultaneous toward-zero
// move on those same intervals, using integer difference-bound propagation.
struct Slack
{
  I lower, upper;  // allowed decrease/increase of distance, in whole DBU
};
constexpr I unlimited = INT64_MAX / 8;
__device__ int towardZero(I x)
{
  return x < 0 ? 1 : (x > 0 ? -1 : 0);
}
__global__ void retractionSlack(View v, int ne, Slack* slack)
{
  const int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= ne) {
    return;
  }
  const auto e = v.edges[id];
  const I d = distance(v, id), g = gap(v, id, d);
  Slack s{0, 0};
  for (int j = e.range_begin; j < e.range_end; ++j) {
    const auto r = v.ranges[j];
    if ((r.has_low && d < r.low2 - g) || (r.has_high && d > r.high2 + g)) {
      continue;
    }
    s.lower = r.has_low ? floorHalf(d - r.low2 + g) : unlimited;
    s.upper = r.has_high ? floorHalf(r.high2 + g - d) : unlimited;
    break;
  }
  slack[id] = s;
}
// Mixed directions create t_src+t_dst <= slack instead of a difference bound.
// Split this capacity deterministically before propagation, so subsequent cap
// reductions can only improve feasibility. Use integer proportional shares.
__device__ I share(I capacity, I own, I other, bool source)
{
  if (capacity >= own + other) {
    return own;
  }
  // Geometry is <=2^40; avoid overflow of capacity*own via double estimate,
  // then enforce an exact complementary split and the bounds in the guard.
  const I first
      = bound(static_cast<I>(double(capacity) * double(source ? own : other)
                             / double(own + other)),
              0,
              capacity);
  return source ? first : capacity - first;
}
__global__ void initialRetractionCaps(View v,
                                      int nn,
                                      const Slack* slack,
                                      I* caps)
{
  const int node = blockIdx.x * blockDim.x + threadIdx.x;
  if (node >= nn) {
    return;
  }
  const int sign = towardZero(v.x[node]);
  I cap = absolute(v.x[node]);
  const auto n = v.csr[node];
  for (int k = n.begin; k < n.end && cap; ++k) {
    const int id = v.incident[k];
    const auto e = v.edges[id];
    const bool source = e.src == node;
    const int other = source ? e.dst : e.src;
    const int other_sign = towardZero(v.x[other]);
    if (sign == other_sign) {
      continue;
    }
    const int effect = source ? -sign : sign;
    const I capacity = effect > 0 ? slack[id].upper : slack[id].lower;
    const I own = absolute(v.x[node]), neighbor = absolute(v.x[other]);
    cap = lo(cap, share(capacity, own, neighbor, source));
  }
  caps[node] = cap;
}
__global__ void propagateRetractionCaps(View v,
                                        int nn,
                                        const Slack* slack,
                                        const I* before,
                                        I* after,
                                        int* changes)
{
  const int node = blockIdx.x * blockDim.x + threadIdx.x;
  if (node >= nn) {
    return;
  }
  I cap = before[node];
  const int sign = towardZero(v.x[node]);
  const auto n = v.csr[node];
  for (int k = n.begin; k < n.end && cap; ++k) {
    const int id = v.incident[k];
    const auto e = v.edges[id];
    const bool source = e.src == node;
    const int other = source ? e.dst : e.src;
    if (sign != towardZero(v.x[other])) {
      continue;
    }
    const int effect = source ? -sign : sign;
    const I capacity = effect > 0 ? slack[id].upper : slack[id].lower;
    cap = lo(cap, before[other] + capacity);
  }
  after[node] = cap;
  if (cap != before[node]) {
    atomicExch(changes, 1);
  }
}
__device__ I proposed(const View& v, const I* caps, int node)
{
  return v.x[node] + towardZero(v.x[node]) * caps[node];
}
// Budget exhaustion is safe: reject any component containing a still-invalid
// proposal. No coordinates are changed during propagation or verification.
__global__ void verifyRetraction(View v,
                                 int ne,
                                 const I* caps,
                                 const int* components,
                                 int* invalid)
{
  const int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= ne) {
    return;
  }
  const auto e = v.edges[id];
  const I d = v.initial[id]
              + 2 * (proposed(v, caps, e.dst) - proposed(v, caps, e.src));
  if (gap(v, id, d) > gap(v, id, distance(v, id))) {
    atomicExch(invalid + components[id], 1);
  }
}
__global__ void commitRetraction(View v,
                                 int nn,
                                 const I* caps,
                                 const int* components,
                                 const int* invalid,
                                 int* moves)
{
  const int node = blockIdx.x * blockDim.x + threadIdx.x;
  if (node >= nn || !caps[node]) {
    return;
  }
  const auto n = v.csr[node];
  if (n.begin < n.end && invalid[components[v.incident[n.begin]]]) {
    return;
  }
  const I candidate = proposed(v, caps, node);
  if (candidate < v.nodes[node].low || candidate > v.nodes[node].up
      || absolute(candidate) >= absolute(v.x[node])) {
    return;
  }
  v.x[node] = candidate;
  atomicAdd(moves, 1);
}
// Equality-connected trims may have opposite outward directions. Translating
// the entire merged group can shorten the total even if a minority of its
// nodes moves farther from zero, as allowed by DEL's LP objective.
struct GroupMove
{
  I delta = 0, shift = 0;
  int root = -1;
};
__device__ bool groupBetter(const GroupMove& a, const GroupMove& b)
{
  if (a.root < 0) {
    return false;
  }
  if (b.root < 0) {
    return true;
  }
  return a.delta < b.delta || (a.delta == b.delta && a.root < b.root);
}
__global__ void initializeGroups(int n,
                                 int* parents,
                                 int* heads,
                                 GroupMove* candidates)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    parents[i] = i;
    heads[i] = -1;
    candidates[i] = GroupMove{};
  }
}
__device__ int groupRoot(int* parents, int node)
{
  int next = atomicAdd(parents + node, 0);
  while (next != node) {
    node = next;
    next = atomicAdd(parents + node, 0);
  }
  return node;
}
__global__ void unionGroups(View v, int ne, const Slack* slack, int* parents)
{
  const int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= ne || slack[id].lower || slack[id].upper) {
    return;
  }
  const auto e = v.edges[id];
  while (true) {
    int a = groupRoot(parents, e.src), b = groupRoot(parents, e.dst);
    if (a == b) {
      return;
    }
    if (a > b) {
      int temporary = a;
      a = b;
      b = temporary;
    }
    // Parents always decrease; concurrent unions cannot create cycles.
    if (atomicCAS(parents + b, b, a) == b) {
      return;
    }
  }
}
__global__ void listGroups(int n,
                           int* parents,
                           int* labels,
                           int* heads,
                           int* next)
{
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) {
    const int root = groupRoot(parents, i);
    labels[i] = root;
    next[i] = atomicExch(heads + root, i);
  }
}
__global__ void groupCandidates(View v,
                                int nn,
                                const Slack* slack,
                                const int* labels,
                                const int* heads,
                                const int* next,
                                GroupMove* candidates)
{
  const int root = blockIdx.x * blockDim.x + threadIdx.x;
  if (root >= nn || heads[root] < 0) {
    return;
  }
  int count = 0;
  I low = -unlimited, up = unlimited, min_x = unlimited, max_x = -unlimited;
  for (int node = heads[root]; node >= 0; node = next[node]) {
    // Keep worst-case work bounded for unusually large merged groups.
    if (++count > 256) {
      return;
    }
    const I x = v.x[node];
    low = hi(low, v.nodes[node].low - x);
    up = lo(up, v.nodes[node].up - x);
    min_x = lo(min_x, x);
    max_x = hi(max_x, x);
    const auto n = v.csr[node];
    for (int k = n.begin; k < n.end; ++k) {
      const int id = v.incident[k];
      const auto e = v.edges[id];
      const bool source = e.src == node;
      if (labels[source ? e.dst : e.src] == root) {
        continue;
      }
      low = hi(low, -(source ? slack[id].upper : slack[id].lower));
      up = lo(up, source ? slack[id].lower : slack[id].upper);
    }
  }
  if (count < 2 || low > up) {
    return;
  }
  // Median minimizes sum(abs(x_i + shift)); integer binary selection avoids
  // sorting or nondeterministic floating sums on the atomic member lists.
  I l = min_x, u = max_x;
  while (l < u) {
    const I middle = l + (u - l) / 2;
    int smaller = 0;
    for (int node = heads[root]; node >= 0; node = next[node]) {
      smaller += v.x[node] <= middle;
    }
    if (smaller >= (count + 1) / 2) {
      u = middle;
    } else {
      l = middle + 1;
    }
  }
  GroupMove candidate;
  candidate.shift = bound(-l, low, up);
  for (int node = heads[root]; node >= 0; node = next[node]) {
    candidate.delta
        += absolute(v.x[node] + candidate.shift) - absolute(v.x[node]);
  }
  if (candidate.delta < 0) {
    candidate.root = root;
    candidates[root] = candidate;
  }
}
__global__ void selectGroups(const OptEdge* edges,
                             const int* order,
                             const int* offsets,
                             const int* labels,
                             const GroupMove* candidates,
                             GroupMove* selected)
{
  __shared__ __align__(16) unsigned char storage[sizeof(GroupMove) * 128];
  auto* best = reinterpret_cast<GroupMove*>(storage);
  const int c = blockIdx.x, t = threadIdx.x;
  GroupMove winner;
  for (int k = offsets[c] + t; k < offsets[c + 1]; k += blockDim.x) {
    const auto e = edges[order[k]];
    const GroupMove a = candidates[labels[e.src]],
                    b = candidates[labels[e.dst]];
    if (groupBetter(a, winner)) {
      winner = a;
    }
    if (groupBetter(b, winner)) {
      winner = b;
    }
  }
  best[t] = winner;
  __syncthreads();
  for (int stride = blockDim.x / 2; stride; stride /= 2) {
    if (t < stride && groupBetter(best[t + stride], best[t])) {
      best[t] = best[t + stride];
    }
    __syncthreads();
  }
  if (!t) {
    selected[c] = best[0];
  }
}
__device__ I groupProposal(const View& v,
                           int node,
                           const int* labels,
                           const GroupMove& selected)
{
  return v.x[node] + (labels[node] == selected.root ? selected.shift : 0);
}
__global__ void verifyGroups(View v,
                             int ne,
                             const int* labels,
                             const int* components,
                             const GroupMove* selected,
                             int* invalid)
{
  const int id = blockIdx.x * blockDim.x + threadIdx.x;
  if (id >= ne) {
    return;
  }
  const auto e = v.edges[id];
  const int c = components[id];
  const auto p = selected[c];
  if (p.root < 0) {
    return;
  }
  const I source = groupProposal(v, e.src, labels, p),
          target = groupProposal(v, e.dst, labels, p);
  const I d = v.initial[id] + 2 * (target - source);
  if (gap(v, id, d) > gap(v, id, distance(v, id)) || source < v.nodes[e.src].low
      || source > v.nodes[e.src].up || target < v.nodes[e.dst].low
      || target > v.nodes[e.dst].up) {
    atomicExch(invalid + c, 1);
  }
}
__global__ void commitGroups(View v,
                             int nn,
                             const int* labels,
                             const int* components,
                             const GroupMove* selected,
                             const int* invalid,
                             int* moves)
{
  const int node = blockIdx.x * blockDim.x + threadIdx.x;
  if (node >= nn || v.csr[node].begin == v.csr[node].end) {
    return;
  }
  const int c = components[v.incident[v.csr[node].begin]];
  const auto p = selected[c];
  if (!invalid[c] && p.root >= 0 && labels[node] == p.root) {
    v.x[node] += p.shift;
    atomicAdd(moves, 1);
  }
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
