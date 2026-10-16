// SPDX-License-Identifier: BSD-3-Clause
// Included inside namespace delgpu; the integer geometry helpers come first.
#include "Postprocess.cuh"

namespace post {
// A bounded large-neighborhood search, not a general-purpose MIP solver.
// Selecting an interval turns each arc into integer difference bounds. Their
// matrix is a network matrix: solve the linear objective exactly by routing
// its dual supplies with successive shortest augmenting paths. All positions,
// costs and feasibility checks use integer DBU; no external CPU solver.
constexpr int joint_nodes = 128;
constexpr int joint_edges = 512;
template <int Nodes, int Edges>
struct Network
{
  static constexpr int network_vertices = Nodes + 1;
  static constexpr int network_arcs = 4 * (Nodes + Edges);
  static_assert(network_arcs < 4095 && Nodes < 32765);
  int16_t head[network_vertices], to[network_arcs], next[network_arcs];
  int16_t capacity[network_arcs], supply[network_vertices];
  I cost[network_arcs], potential[network_vertices];
  int vertices, arcs = 0;

  __device__ void initialize(int n)
  {
    arcs = 0;
    vertices = n;
    for (int i = 0; i < n; ++i) {
      head[i] = -1;
      supply[i] = 0;
      potential[i] = 0;
    }
  }
  __device__ void constraint(int a, int b, I weight)
  {
    to[arcs] = b;
    cost[arcs] = weight;
    capacity[arcs] = vertices + 1;
    next[arcs] = head[a];
    head[a] = arcs++;
    to[arcs] = a;
    cost[arcs] = -weight;
    capacity[arcs] = 0;
    next[arcs] = head[b];
    head[b] = arcs++;
  }
  __device__ bool solve()
  {
    // Find feasible potentials; a negative cycle proves this branch choice
    // infeasible. Starting from the incumbent is cheap for its own branch.
    for (int round = 0; round < vertices; ++round) {
      bool changed = false;
      for (int u = 0; u < vertices; ++u) {
        for (int a = head[u]; a >= 0; a = next[a]) {
          const int v = to[a];
          if (capacity[a] && potential[v] > potential[u] + cost[a]) {
            potential[v] = potential[u] + cost[a];
            changed = true;
          }
        }
      }
      if (!changed) {
        break;
      }
      if (round + 1 == vertices) {
        return false;
      }
    }
    for (int iteration = 0; iteration < vertices; ++iteration) {
      int source = -1;
      for (int i = 0; i < vertices; ++i) {
        if (supply[i] > 0) {
          source = i;
          break;
        }
      }
      if (source < 0) {
        return true;
      }
      I distances[network_vertices];
      int previous[network_vertices];
      bool visited[network_vertices];
      for (int i = 0; i < vertices; ++i) {
        distances[i] = unlimited;
        previous[i] = -1;
        visited[i] = false;
      }
      distances[source] = 0;
      for (int step = 0; step < vertices; ++step) {
        int u = -1;
        for (int i = 0; i < vertices; ++i) {
          if (!visited[i] && (u < 0 || distances[i] < distances[u])) {
            u = i;
          }
        }
        if (u < 0 || distances[u] == unlimited) {
          return false;  // finite domain arcs should connect every vertex
        }
        visited[u] = true;
        for (int a = head[u]; a >= 0; a = next[a]) {
          const int v = to[a];
          const I reduced = cost[a] + potential[u] - potential[v];
          if (capacity[a] && !visited[v]
              && distances[v] > distances[u] + reduced) {
            if (reduced < 0) {
              return false;
            }
            distances[v] = distances[u] + reduced;
            previous[v] = a;
          }
        }
      }
      int target = -1;
      for (int i = 0; i < vertices; ++i) {
        if (supply[i] < 0 && (target < 0 || distances[i] < distances[target])) {
          target = i;
        }
      }
      if (target < 0) {
        return false;
      }
      int amount
          = supply[source] < -supply[target] ? supply[source] : -supply[target];
      for (int v = target; v != source; v = to[previous[v] ^ 1]) {
        if (previous[v] < 0) {
          return false;
        }
        amount
            = amount < capacity[previous[v]] ? amount : capacity[previous[v]];
      }
      if (!amount) {
        return false;
      }
      for (int v = target; v != source; v = to[previous[v] ^ 1]) {
        capacity[previous[v]] -= amount;
        capacity[previous[v] ^ 1] += amount;
      }
      supply[source] -= amount;
      supply[target] += amount;
      const I anchor = potential[vertices - 1] + distances[vertices - 1];
      for (int i = 0; i < vertices; ++i) {
        potential[i] += distances[i] - anchor;
      }
    }
    return false;
  }
};
// One warp cooperates on the same shared-memory network. Large components
// avoid per-thread array spills, and shortest-path selection uses warp votes.
template <int Nodes, int Edges>
struct WarpNetwork : Network<Nodes, Edges>
{
  static constexpr int nv = Nodes + 1;
  static constexpr unsigned mask = 0xffffffff;
  I next_potential[nv];
  unsigned long long keys[nv];
  bool visited[nv];
  __device__ bool solveWarp()
  {
    auto& net = *this;
    const int lane = threadIdx.x;
    for (int round = 0; round < net.vertices; ++round) {
      for (int i = lane; i < net.vertices; i += 32) {
        next_potential[i] = net.potential[i];
      }
      __syncwarp();
      for (int u = lane; u < net.vertices; u += 32) {
        for (int a = net.head[u]; a >= 0; a = net.next[a]) {
          if (net.capacity[a]) {
            atomicMin(reinterpret_cast<long long*>(next_potential + net.to[a]),
                      static_cast<long long>(net.potential[u] + net.cost[a]));
          }
        }
      }
      __syncwarp();
      bool changed = false;
      for (int i = lane; i < net.vertices; i += 32) {
        changed |= next_potential[i] != net.potential[i];
        net.potential[i] = next_potential[i];
      }
      __syncwarp();
      if (!__any_sync(mask, changed)) {
        break;
      }
      if (round + 1 == net.vertices) {
        return false;
      }
    }
    // 12 low bits encode the predecessor arc. Geometry is limited to 2^40,
    // so a simple path's reduced distance fits in the remaining 52 bits.
    // Atomic comparison updates distance and predecessor together, including
    // parallel arcs, without a race between two independent stores.
    constexpr unsigned long long infinity = ~0ULL >> 12;
    for (int iteration = 0; iteration < net.vertices; ++iteration) {
      int source = -1;
      if (!lane) {
        for (int i = 0; i < net.vertices; ++i) {
          if (net.supply[i] > 0) {
            source = i;
            break;
          }
        }
      }
      source = __shfl_sync(mask, source, 0);
      if (source < 0) {
        return true;
      }
      for (int i = lane; i < net.vertices; i += 32) {
        keys[i] = i == source ? 4095 : ~0ULL;
        visited[i] = false;
      }
      __syncwarp();
      for (int step = 0; step < net.vertices; ++step) {
        unsigned long long minimum = infinity;
        int u = -1;
        for (int i = lane; i < net.vertices; i += 32) {
          const auto distance = keys[i] >> 12;
          if (!visited[i] && (u < 0 || distance < minimum)) {
            minimum = distance;
            u = i;
          }
        }
        for (int offset = 16; offset; offset /= 2) {
          const auto other_distance = __shfl_down_sync(mask, minimum, offset);
          const int other = __shfl_down_sync(mask, u, offset);
          if (lane + offset < 32 && other >= 0
              && (u < 0 || other_distance < minimum
                  || (other_distance == minimum && other < u))) {
            minimum = other_distance;
            u = other;
          }
        }
        u = __shfl_sync(mask, u, 0);
        minimum = __shfl_sync(mask, minimum, 0);
        if (u < 0 || minimum == infinity) {
          return false;
        }
        if (!lane) {
          visited[u] = true;
        }
        __syncwarp();
        int a = net.head[u];
        for (int skip = 0; skip < lane && a >= 0; ++skip) {
          a = net.next[a];
        }
        bool invalid = false;
        while (a >= 0) {
          const int v = net.to[a];
          if (net.capacity[a] && !visited[v]) {
            const I reduced = net.cost[a] + net.potential[u] - net.potential[v];
            if (reduced < 0
                || static_cast<unsigned long long>(reduced)
                       >= infinity - minimum) {
              invalid = true;
            } else {
              atomicMin(keys + v, ((minimum + reduced) << 12) | a);
            }
          }
          for (int skip = 0; skip < 32 && a >= 0; ++skip) {
            a = net.next[a];
          }
        }
        __syncwarp();
        if (__any_sync(mask, invalid)) {
          return false;
        }
      }
      int target = -1;
      bool invalid = false;
      if (!lane) {
        for (int i = 0; i < net.vertices; ++i) {
          if (net.supply[i] < 0
              && (target < 0 || (keys[i] >> 12) < (keys[target] >> 12))) {
            target = i;
          }
        }
        if (target < 0) {
          invalid = true;
        }
        if (!invalid) {
          int amount = net.supply[source] < -net.supply[target]
                           ? net.supply[source]
                           : -net.supply[target];
          for (int v = target; v != source;) {
            const int a = keys[v] & 4095;
            if (a >= net.arcs) {
              invalid = true;
              break;
            }
            amount = amount < net.capacity[a] ? amount : net.capacity[a];
            v = net.to[a ^ 1];
          }
          if (amount <= 0) {
            invalid = true;
          }
          if (!invalid) {
            for (int v = target; v != source;) {
              const int a = keys[v] & 4095;
              net.capacity[a] -= amount;
              net.capacity[a ^ 1] += amount;
              v = net.to[a ^ 1];
            }
            net.supply[source] -= amount;
            net.supply[target] += amount;
          }
        }
      }
      if (__any_sync(mask, invalid)) {
        return false;
      }
      __syncwarp();
      const I anchor
          = net.potential[net.vertices - 1] + (keys[net.vertices - 1] >> 12);
      __syncwarp();
      for (int i = lane; i < net.vertices; i += 32) {
        net.potential[i] += I(keys[i] >> 12) - anchor;
      }
      __syncwarp();
    }
    return false;
  }
};
struct JointCandidate
{
  Score delta;
  bool valid = false;
};
struct JointComponents
{
  const int* nodes;
  const int* offsets;
  const int* local;
  const int* edges;
  const int* edge_offsets;
};
__device__ int nearestRange(const View& v, int id, I d)
{
  I best = unlimited;
  int choice = v.edges[id].range_begin;
  for (int j = v.edges[id].range_begin; j < v.edges[id].range_end; ++j) {
    const auto r = v.ranges[j];
    const I g
        = hi(0, hi(r.has_low ? r.low2 - d : 0, r.has_high ? d - r.high2 : 0));
    if (g < best) {
      best = g;
      choice = j;
    }
  }
  return choice;
}
// One GPU thread owns one component/interval assignment. Components and all
// candidate positions are disjoint; selection/commit use a separate kernel.
template <int Nodes, int Edges>
__global__ void jointCandidates(View v,
                                JointComponents components,
                                const int* component_ids,
                                int count,
                                int nn,
                                int trials,
                                int round,
                                const int* done,
                                I* positions,
                                JointCandidate* candidates)
{
  const int job = blockIdx.x * blockDim.x + threadIdx.x;
  if (job >= count * trials) {
    return;
  }
  const int c = component_ids[job / trials], trial = job % trials;
  const int output = c * trials + trial;
  if (done[c]) {
    return;
  }
  const int begin = components.offsets[c], end = components.offsets[c + 1];
  const int n = end - begin;
  const int eb = components.edge_offsets[c],
            ee = components.edge_offsets[c + 1];
  if (n > Nodes || ee - eb > Edges) {
    return;
  }
  candidates[output] = JointCandidate{};
  int combinations = 1, alternatives = 0;
  bool violated = false, nonzero = false;
  for (int k = begin; k < end; ++k) {
    nonzero |= v.x[components.nodes[k]] != 0;
  }
  for (int k = eb; k < ee; ++k) {
    const int id = components.edges[k];
    const auto e = v.edges[id];
    const int choices = e.range_end - e.range_begin;
    combinations
        = combinations > trials / choices ? trials : combinations * choices;
    alternatives += choices - 1;
    violated |= gap(v, id, distance(v, id)) > 0;
  }
  if ((!nonzero && !violated) || (trial == 1 && !violated)) {
    return;
  }
  // Trial 0 compacts the incumbent; 1 attempts full legality. Trials 2/3
  // jointly reselect intervals near zero/half displacement to cross barriers
  // that a single-edge flip cannot cross. The remaining budget enumerates
  // all assignments when small, otherwise single-edge flips across rounds.
  const bool seed = trial == 2 || trial == 3;
  int code = trial < 4 ? 0 : trial - 3;
  const bool enumerate = combinations <= trials - 3;
  if ((seed && !alternatives)
      || (trial >= 4
          && (enumerate ? code >= combinations : trial - 4 >= alternatives))) {
    return;
  }
  int flip = alternatives && trial >= 4
                 ? ((trial - 4) + round * (trials - 4)) % alternatives
                 : -1;
  Network<Nodes, Edges> network;
  network.initialize(n + 1);
  for (int i = 0; i < n; ++i) {
    const int id = components.nodes[begin + i];
    const auto node = v.nodes[id];
    I low = node.low, up = node.up;
    // Real trim domains are outward-only. For synthetic bidirectional nodes
    // restrict this neighborhood to the incumbent's orthant, so |x| is linear.
    if (low < 0 && up > 0) {
      if (v.x[id] < 0) {
        up = 0;
      } else {
        low = 0;
      }
    }
    const int weight = low == up ? 0 : (low >= 0 ? 1 : -1);
    network.supply[i] = weight;
    network.supply[n] -= weight;
    network.potential[i] = v.x[id];
    network.constraint(n, i, up);
    network.constraint(i, n, -low);
  }
  for (int k = eb; k < ee; ++k) {
    const int id = components.edges[k];
    const auto e = v.edges[id];
    const int choices = e.range_end - e.range_begin;
    int offset = 0;
    if (trial >= 4 && enumerate) {
      offset = code % choices;
      code /= choices;
    } else if (trial >= 4) {
      if (flip >= 0 && flip < choices - 1) {
        offset = flip + 1;
        flip = -1;
      } else if (flip >= 0) {
        flip -= choices - 1;
      }
    }
    const I target
        = !seed ? distance(v, id)
                : v.initial[id] + (trial == 2 ? 0 : v.x[e.dst] - v.x[e.src]);
    const int selected
        = e.range_begin
          + (nearestRange(v, id, target) - e.range_begin + offset) % choices;
    const auto r = v.ranges[selected];
    const I g = trial == 1 ? 0 : gap(v, id, distance(v, id));
    const int a = components.local[e.src], b = components.local[e.dst];
    if (r.has_high) {
      network.constraint(a, b, floorHalf(r.high2 + g - v.initial[id]));
    }
    if (r.has_low) {
      network.constraint(b, a, -ceilHalf(r.low2 - g - v.initial[id]));
    }
  }
  if (!network.solve()) {
    return;
  }
  Score delta;
  for (int i = 0; i < n; ++i) {
    const int id = components.nodes[begin + i];
    const I x = network.potential[i] - network.potential[n];
    if (x < v.nodes[id].low || x > v.nodes[id].up) {
      return;
    }
    delta.extent += absolute(x) - absolute(v.x[id]);
  }
  if (delta.extent > 0) {
    return;
  }
  // Exact union-of-intervals guard, independent of chosen branch and network
  // feasibility. Every original edge residual must be nonincreasing.
  for (int k = eb; k < ee; ++k) {
    const int id = components.edges[k];
    const auto e = v.edges[id];
    const I d = v.initial[id]
                + 2
                      * (network.potential[components.local[e.dst]]
                         - network.potential[components.local[e.src]]);
    const I before = gap(v, id, distance(v, id)), after = gap(v, id, d);
    if (after > before) {
      return;
    }
    addDelta(delta, before, after, v.tolerance);
  }
  if (!better(delta, Score{})) {
    return;
  }
  for (int i = 0; i < n; ++i) {
    const int id = components.nodes[begin + i];
    positions[size_t(trial) * nn + id]
        = network.potential[i] - network.potential[n];
  }
  candidates[output] = {delta, true};
}
__global__ void commitJoint(View v,
                            JointComponents components,
                            int count,
                            int nn,
                            int trials,
                            const I* positions,
                            const JointCandidate* candidates,
                            int* moves)
{
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= count) {
    return;
  }
  int best = -1;
  Score delta;
  for (int t = 0; t < trials; ++t) {
    const auto p = candidates[c * trials + t];
    if (p.valid && better(p.delta, delta)) {
      best = t;
      delta = p.delta;
    }
  }
  if (best < 0) {
    return;
  }
  for (int k = components.offsets[c]; k < components.offsets[c + 1]; ++k) {
    const int id = components.nodes[k];
    v.x[id] = positions[size_t(best) * nn + id];
  }
  atomicAdd(moves, 1);
}
// Bounded disjunctive branch-and-bound on already legal components. Relaxing
// an unassigned union to its interval hull gives an exact network lower bound.
// Branch only on an edge violated by that relaxed optimum, and prune when its
// lower bound cannot beat the best feasible length. Depth/state limits retain
// the incumbent; they do not claim general MIP optimality.
template <int Nodes, int Edges>
struct BoundWorkspace
{
  WarpNetwork<Nodes, Edges> network;
  int selected[Edges];
  int stack_edge[64], stack_first[64], stack_next[64];
  int depth, evaluations;
  I incumbent, best;
  bool eligible, impossible, exhausted, depth_limited, stop, outward;
};
template <int Nodes, int Edges>
__global__ void jointBound(View v,
                           JointComponents components,
                           const int* component_ids,
                           int count,
                           int nn,
                           int trials,
                           I* positions,
                           JointCandidate* candidates,
                           int* counters,
                           int* done)
{
  const int index = blockIdx.x;
  if (index >= count) {
    return;
  }
  const int c = component_ids[index], lane = threadIdx.x;
  // Broadcast before lane 0 can publish a newly completed component below.
  const int finished = __shfl_sync(0xffffffff, lane == 0 ? done[c] : 0, 0);
  if (finished) {
    return;
  }
  __shared__ __align__(
      16) unsigned char storage[sizeof(BoundWorkspace<Nodes, Edges>)];
  static_assert(sizeof(BoundWorkspace<Nodes, Edges>) <= 48 * 1024);
  auto& w = *reinterpret_cast<BoundWorkspace<Nodes, Edges>*>(storage);
  auto& network = w.network;
  const int begin = components.offsets[c],
            n = components.offsets[c + 1] - begin;
  const int eb = components.edge_offsets[c],
            ne = components.edge_offsets[c + 1] - eb;
  if (!lane) {
    w.incumbent = 0;
    w.eligible = false;
    w.outward = true;
    for (int i = 0; i < n; ++i) {
      const int id = components.nodes[begin + i];
      w.incumbent += absolute(v.x[id]);
      w.outward &= !(v.nodes[id].low < 0 && v.nodes[id].up > 0);
    }
    bool legal = true;
    for (int k = 0; k < ne; ++k) {
      const int id = components.edges[eb + k];
      legal &= gap(v, id, distance(v, id)) == 0;
      w.eligible |= v.edges[id].range_end - v.edges[id].range_begin > 1;
      w.selected[k] = -1;
    }
    w.eligible &= legal && w.incumbent > 0;
    if (legal && !w.incumbent) {
      done[c] = 1;
    }
    w.best = w.incumbent;
    for (int t = 0; t < trials; ++t) {
      const auto p = candidates[c * trials + t];
      if (p.valid) {
        w.best = lo(w.best, w.incumbent + p.delta.extent);
      }
    }
    w.depth = w.evaluations = 0;
    w.exhausted = w.depth_limited = w.stop = false;
  }
  __syncwarp();
  if (!w.eligible) {
    return;
  }
  for (int state = 0; state < trials; ++state) {
    if (!lane) {
      ++w.evaluations;
      network.initialize(n + 1);
      for (int i = 0; i < n; ++i) {
        const int id = components.nodes[begin + i];
        auto node = v.nodes[id];
        if (node.low < 0 && node.up > 0) {
          if (v.x[id] < 0) {
            node.up = 0;
          } else {
            node.low = 0;
          }
        }
        const int weight = node.low == node.up ? 0 : (node.low >= 0 ? 1 : -1);
        network.supply[i] = weight;
        network.supply[n] -= weight;
        network.potential[i] = v.x[id];
        network.constraint(n, i, node.up);
        network.constraint(i, n, -node.low);
      }
      w.impossible = false;
      for (int k = 0; k < ne; ++k) {
        const int id = components.edges[eb + k];
        const auto e = v.edges[id];
        I low = unlimited, up = -unlimited;
        const int first = w.selected[k] < 0 ? e.range_begin : w.selected[k];
        const int last = w.selected[k] < 0 ? e.range_end : w.selected[k] + 1;
        for (int j = first; j < last; ++j) {
          const auto r = v.ranges[j];
          const I l = r.has_low ? ceilHalf(r.low2 - v.initial[id]) : -unlimited;
          const I u
              = r.has_high ? floorHalf(r.high2 - v.initial[id]) : unlimited;
          if (l <= u) {
            low = lo(low, l);
            up = hi(up, u);
          }
        }
        if (low > up) {
          w.impossible = true;
          break;
        }
        const int a = components.local[e.src], b = components.local[e.dst];
        if (up < unlimited) {
          network.constraint(a, b, up);
        }
        if (low > -unlimited) {
          network.constraint(b, a, -low);
        }
      }
    }
    __syncwarp();
    const bool solved = !w.impossible && network.solveWarp();
    if (!lane) {
      bool branch = false;
      if (solved) {
        I extent = 0;
        for (int i = 0; i < n; ++i) {
          extent += absolute(network.potential[i] - network.potential[n]);
        }
        if (extent < w.best) {
          I largest_gap = 0;
          int split = -1;
          for (int k = 0; k < ne; ++k) {
            const int id = components.edges[eb + k];
            const auto e = v.edges[id];
            const I d = v.initial[id]
                        + 2
                              * (network.potential[components.local[e.dst]]
                                 - network.potential[components.local[e.src]]);
            const I g = gap(v, id, d);
            if (g > largest_gap) {
              largest_gap = g;
              split = k;
            }
          }
          if (split < 0) {
            bool valid = true;
            for (int i = 0; i < n; ++i) {
              const auto node = v.nodes[components.nodes[begin + i]];
              const I x = network.potential[i] - network.potential[n];
              valid &= x >= node.low && x <= node.up;
            }
            if (valid) {
              w.best = extent;
              for (int i = 0; i < n; ++i) {
                positions[size_t(nn) + components.nodes[begin + i]]
                    = network.potential[i] - network.potential[n];
              }
              candidates[c * trials + 1]
                  = {Score{0, 0, 0, w.best - w.incumbent}, true};
            }
          } else if (w.depth < 64 && w.selected[split] < 0) {
            const int id = components.edges[eb + split];
            const auto e = v.edges[id];
            const I d = v.initial[id]
                        + 2
                              * (network.potential[components.local[e.dst]]
                                 - network.potential[components.local[e.src]]);
            const int first = nearestRange(v, id, d);
            w.stack_edge[w.depth] = split;
            w.stack_first[w.depth] = first - e.range_begin;
            w.stack_next[w.depth++] = 1;
            w.selected[split] = first;
            branch = true;
          } else {
            w.depth_limited = true;
          }
        }
      }
      if (!branch) {
        while (w.depth) {
          const int top = w.depth - 1, k = w.stack_edge[top];
          const auto e = v.edges[components.edges[eb + k]];
          const int choices = e.range_end - e.range_begin;
          if (w.stack_next[top] < choices) {
            w.selected[k]
                = e.range_begin
                  + (w.stack_first[top] + w.stack_next[top]++) % choices;
            break;
          }
          w.selected[k] = -1;
          --w.depth;
        }
        if (!w.depth) {
          w.exhausted = w.stop = true;
        }
      }
    }
    __syncwarp();
    if (w.stop) {
      break;
    }
  }
  if (!lane) {
    atomicAdd(counters, w.evaluations);
    atomicAdd(counters + ((w.exhausted && !w.depth_limited) ? 1 : 2), 1);
    if (w.exhausted && !w.depth_limited && w.outward) {
      done[c] = 1;
    }
  }
}
}  // namespace post
