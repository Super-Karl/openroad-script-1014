// SPDX-License-Identifier: BSD-3-Clause
// Included inside namespace delgpu. Reuse exact geometry and numeric helpers.
#include "JointPostprocess.cuh"

namespace post {
// Return the greatest admissible nonnegative integer step <= cap for one
// crossing arc. With jumps, intersect against its entire expanded interval
// union; without jumps, retain the nearest expanded interval as in EMA.
__device__ I emaEdgeStep(const View& v, int id, int effect, I cap, bool jumps)
{
  const I d = distance(v, id), g = gap(v, id, d);
  const auto e = v.edges[id];
  const int first = jumps ? e.range_begin : nearestRange(v, id, d);
  const int end = jumps ? e.range_end : first + 1;
  I best = 0;
  for (int j = first; j < end; ++j) {
    const auto r = v.ranges[j];
    I low = 0, up = cap;
    if (effect > 0) {
      if (r.has_low) {
        low = hi(low, ceilHalf(r.low2 - g - d));
      }
      if (r.has_high) {
        up = lo(up, floorHalf(r.high2 + g - d));
      }
    } else {
      if (r.has_high) {
        low = hi(low, ceilHalf(d - r.high2 - g));
      }
      if (r.has_low) {
        up = lo(up, floorHalf(d - r.low2 + g));
      }
    }
    if (low <= up) {
      best = hi(best, up);
    }
  }
  return best;
}
// Directed zero-integer-slack closure, matching TightTree.cpp's two
// directions, with retained CSR instead of rescanning every constraint.
__device__ int emaTree(const View& v,
                       int pivot,
                       int direction,
                       int generation,
                       int* member,
                       int* queue)
{
  int count = 1;
  queue[0] = pivot;
  member[pivot] = generation;
  for (int head = 0; head < count; ++head) {
    const int node = queue[head];
    const auto csr = v.csr[node];
    for (int k = csr.begin; k < csr.end; ++k) {
      const int id = v.incident[k];
      const auto e = v.edges[id];
      const int other = e.src == node ? e.dst : e.src;
      if (member[other] == generation) {
        continue;
      }
      const int effect = e.src == node ? -direction : direction;
      if (!emaEdgeStep(v, id, effect, 1, false)) {
        member[other] = generation;
        queue[count++] = other;
      }
    }
  }
  return count;
}
__device__ I emaStep(const View& v,
                     int direction,
                     int count,
                     const int* queue,
                     const int* member,
                     int generation,
                     bool jumps)
{
  I cap = unlimited, slope = 0;
  int boundary = 0;
  for (int i = 0; i < count; ++i) {
    const int node = queue[i];
    const I x = v.x[node];
    cap = lo(cap, direction > 0 ? v.nodes[node].up - x : x - v.nodes[node].low);
    // Stop at the first absolute-value breakpoint. This equals the paper's
    // signed linear objective on real outward-only domains, and also handles
    // artificial bidirectional domains without crossing a sign unnoticed.
    if (direction * x < 0) {
      cap = lo(cap, absolute(x));
    }
    slope += x ? direction * (x > 0 ? 1 : -1) : 1;
    boundary += v.csr[node].end - v.csr[node].begin;
  }
  if (cap <= 0 || slope >= 0) {
    return 0;
  }
  // Descending projections find the greatest point in the intersection of
  // one-dimensional interval unions. Every reduction lands on an interval
  // endpoint; a finite scan budget rejects unfinished proposals safely.
  for (int pass = 0; pass <= 3 * boundary + 1; ++pass) {
    const I before = cap;
    for (int i = 0; i < count && cap; ++i) {
      const int node = queue[i];
      const auto csr = v.csr[node];
      for (int k = csr.begin; k < csr.end && cap; ++k) {
        const int id = v.incident[k];
        const auto e = v.edges[id];
        const bool source = member[e.src] == generation;
        const bool target = member[e.dst] == generation;
        if (source == target) {
          continue;
        }
        const int effect = target ? direction : -direction;
        cap = emaEdgeStep(v, id, effect, cap, jumps);
      }
    }
    if (!cap || cap == before) {
      return cap;
    }
  }
  return 0;
}
__device__ bool emaCommit(View v,
                          int direction,
                          int count,
                          const int* queue,
                          const int* member,
                          int generation,
                          I step,
                          bool& interval_jump)
{
  if (!step) {
    return false;
  }
  I delta = 0;
  for (int i = 0; i < count; ++i) {
    const int node = queue[i];
    const I x = v.x[node] + direction * step;
    if (x < v.nodes[node].low || x > v.nodes[node].up) {
      return false;
    }
    delta += absolute(x) - absolute(v.x[node]);
    const auto csr = v.csr[node];
    for (int k = csr.begin; k < csr.end; ++k) {
      const int id = v.incident[k];
      const auto e = v.edges[id];
      const int effect
          = direction
            * ((member[e.dst] == generation) - (member[e.src] == generation));
      if (effect
          && gap(v, id, distance(v, id) + 2 * effect * step)
                 > gap(v, id, distance(v, id))) {
        return false;
      }
      if (effect && emaEdgeStep(v, id, effect, step, false) < step) {
        interval_jump = true;
      }
    }
  }
  if (delta >= 0) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    v.x[queue[i]] += direction * step;
  }
  return true;
}
__global__ void emaIsolated(View v, int nn)
{
  const int node = blockIdx.x * blockDim.x + threadIdx.x;
  if (node < nn && v.csr[node].begin == v.csr[node].end) {
    v.x[node] = 0;
  }
}
// Independent components own all their coordinates, queue and membership
// cells. No shared-neighbor races, per-pivot kernel launches or host polls.
// Workspaces are O(nodes), with no 128-node component cutoff.
__global__ void emaComponents(View v,
                              const int* nodes,
                              const int* offsets,
                              int components,
                              int sweeps,
                              bool jumps,
                              int* members,
                              int* queues,
                              unsigned long long* counters,
                              const int* active)
{
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= components || (active && !active[c])) {
    return;
  }
  const int begin = offsets[c], end = offsets[c + 1], n = end - begin;
  int* queue = queues + begin;
  int generation = 0;
  unsigned long long moves = 0, pivots = 0, jump_moves = 0;
  const auto budget = static_cast<unsigned long long>(sweeps) * n;
  while (moves < budget) {
    bool improved = false;
    for (int k = begin; k < end; ++k) {
      const int pivot = nodes[k];
      const I x = v.x[pivot];
      if (!x) {
        continue;
      }
      ++pivots;
      const int direction = x > 0 ? -1 : 1;
      if (generation >= INT_MAX - 2) {
        for (int i = begin; i < end; ++i) {
          members[nodes[i]] = 0;
        }
        generation = 0;
      }
      const int count
          = emaTree(v, pivot, direction, ++generation, members, queue);
      I step = emaStep(v, direction, count, queue, members, generation, jumps);
      bool interval_jump = false;
      if (emaCommit(v,
                    direction,
                    count,
                    queue,
                    members,
                    generation,
                    step,
                    interval_jump)) {
        ++moves;
        improved = true;
        jump_moves += interval_jump;
        break;
      }
      // A tight relation in the old branch need not remain tight after a legal
      // interval jump. Try the pivot alone as a second, cheap neighborhood.
      if (jumps && count > 1) {
        queue[0] = pivot;
        members[pivot] = ++generation;
        step = emaStep(v, direction, 1, queue, members, generation, true);
        interval_jump = false;
        if (emaCommit(v,
                      direction,
                      1,
                      queue,
                      members,
                      generation,
                      step,
                      interval_jump)) {
          ++moves;
          jump_moves += interval_jump;
          improved = true;
          break;
        }
      }
    }
    if (!improved) {
      break;
    }
  }
  atomicAdd(counters, moves);
  atomicAdd(counters + 1, pivots);
  atomicAdd(counters + 2, jump_moves);
  atomicAdd(counters + 3, static_cast<unsigned long long>(moves == budget));
}
// LEO-style greedy interval seeding. Domain-bound feasibility is a cheap
// difference-constraint propagation, not a min-cost network solve. Every
// trial includes all previously accepted intervals. Unlike LEO, a component
// with any unaccepted arc is discarded, preserving the input residual budget.
__device__ Slack emaRangeBounds(const View& v, int id, int range, I gap_cap)
{
  const auto r = v.ranges[range];
  return {
      r.has_low ? ceilHalf(r.low2 - gap_cap - v.initial[id]) : -unlimited,
      r.has_high ? floorHalf(r.high2 + gap_cap - v.initial[id]) : unlimited};
}
__device__ bool emaFeasible(const View& v,
                            const int* nodes,
                            int nn,
                            const int* order,
                            int ne,
                            const int* chosen,
                            const Slack* bounds,
                            I* lower,
                            I* upper,
                            bool both)
{
  for (int i = 0; i < nn; ++i) {
    const int node = nodes[i];
    lower[node] = v.nodes[node].low;
    if (both) {
      upper[node] = v.nodes[node].up;
    }
  }
  for (int pass = 0; pass < nn; ++pass) {
    bool changed = false;
    for (int i = 0; i < ne; ++i) {
      const int id = order[i];
      if (chosen[id] < 0) {
        continue;
      }
      const auto e = v.edges[id];
      const auto b = bounds[id];
      if (b.lower > -unlimited && lower[e.dst] < lower[e.src] + b.lower) {
        lower[e.dst] = lower[e.src] + b.lower;
        changed = true;
        if (lower[e.dst] > v.nodes[e.dst].up) {
          return false;
        }
      }
      if (b.upper < unlimited && lower[e.src] < lower[e.dst] - b.upper) {
        lower[e.src] = lower[e.dst] - b.upper;
        changed = true;
        if (lower[e.src] > v.nodes[e.src].up) {
          return false;
        }
      }
    }
    if (!changed) {
      break;
    }
    if (pass + 1 == nn) {
      return false;
    }
  }
  if (!both) {
    return true;
  }
  for (int pass = 0; pass < nn; ++pass) {
    bool changed = false;
    for (int i = 0; i < ne; ++i) {
      const int id = order[i];
      if (chosen[id] < 0) {
        continue;
      }
      const auto e = v.edges[id];
      const auto b = bounds[id];
      if (b.lower > -unlimited && upper[e.src] > upper[e.dst] - b.lower) {
        upper[e.src] = upper[e.dst] - b.lower;
        changed = true;
        if (upper[e.src] < v.nodes[e.src].low) {
          return false;
        }
      }
      if (b.upper < unlimited && upper[e.dst] > upper[e.src] + b.upper) {
        upper[e.dst] = upper[e.src] + b.upper;
        changed = true;
        if (upper[e.dst] < v.nodes[e.dst].low) {
          return false;
        }
      }
    }
    if (!changed) {
      break;
    }
    if (pass + 1 == nn) {
      return false;
    }
  }
  return true;
}
__device__ bool emaArcLess(const View& v, int a, int b, const I* keys)
{
  if (v.edges[a].type != v.edges[b].type) {
    return v.edges[a].type < v.edges[b].type;
  }
  return keys[a] != keys[b] ? keys[a] < keys[b] : a < b;
}
__global__ void emaSeed(View v,
                        JointComponents components,
                        int count,
                        Slack* bounds,
                        int* chosen,
                        int* order,
                        I* keys,
                        I* lower,
                        I* upper,
                        int* active,
                        unsigned long long* counters)
{
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= count) {
    return;
  }
  active[c] = 0;
  const int nb = components.offsets[c], nn = components.offsets[c + 1] - nb;
  const int eb = components.edge_offsets[c],
            ne = components.edge_offsets[c + 1] - eb;
  const int* nodes = components.nodes + nb;
  int* edges = order + eb;
  I old_extent = 0;
  for (int i = 0; i < nn; ++i) {
    old_extent += absolute(v.x[nodes[i]]);
  }
  if (!old_extent) {
    return;
  }
  for (int i = 0; i < ne; ++i) {
    const int id = components.edges[eb + i];
    const auto e = v.edges[id];
    edges[i] = id;
    chosen[id] = 0;
    const I cap = gap(v, id, distance(v, id));
    Slack hull{unlimited, -unlimited};
    for (int j = e.range_begin; j < e.range_end; ++j) {
      const auto b = emaRangeBounds(v, id, j, cap);
      if (b.lower <= b.upper) {
        hull.lower = lo(hull.lower, b.lower);
        hull.upper = hi(hull.upper, b.upper);
      }
    }
    bounds[id] = hull;
  }
  const bool relaxed = emaFeasible(
      v, nodes, nn, edges, ne, chosen, bounds, lower, upper, true);
  for (int i = 0; i < ne; ++i) {
    const int id = edges[i];
    const auto e = v.edges[id];
    const I cap = gap(v, id, distance(v, id));
    I key = unlimited;
    const I ls = relaxed ? lower[e.src] : v.nodes[e.src].low,
            lt = relaxed ? lower[e.dst] : v.nodes[e.dst].low,
            us = relaxed ? upper[e.src] : v.nodes[e.src].up,
            ut = relaxed ? upper[e.dst] : v.nodes[e.dst].up;
    for (int j = e.range_begin; j < e.range_end; ++j) {
      const auto b = emaRangeBounds(v, id, j, cap);
      if (b.lower > -unlimited) {
        key = lo(key, ut - ls - b.lower);
      }
      if (b.upper < unlimited) {
        key = lo(key, us - lt + b.upper);
      }
    }
    keys[id] = key;
    chosen[id] = -1;
  }
  // Stable insertion sort over the small component, keyed by type/slack/ID.
  for (int i = 1; i < ne; ++i) {
    const int id = edges[i];
    int j = i;
    while (j > 0 && emaArcLess(v, id, edges[j - 1], keys)) {
      edges[j] = edges[j - 1];
      --j;
    }
    edges[j] = id;
  }
  for (int i = 0; i < ne; ++i) {
    const int id = edges[i];
    const auto e = v.edges[id];
    const I cap = gap(v, id, distance(v, id));
    I last_distance = -1;
    int last_id = -1;
    bool accepted = false;
    for (int rank = e.range_begin; rank < e.range_end; ++rank) {
      int best = -1;
      I best_distance = unlimited;
      for (int j = e.range_begin; j < e.range_end; ++j) {
        const auto b = emaRangeBounds(v, id, j, cap);
        if (b.lower > b.upper) {
          continue;
        }
        const I d = hi(0, hi(b.lower, -b.upper));
        if ((d > last_distance || (d == last_distance && j > last_id))
            && (d < best_distance
                || (d == best_distance && (best < 0 || j < best)))) {
          best = j;
          best_distance = d;
        }
      }
      if (best < 0) {
        break;
      }
      last_distance = best_distance;
      last_id = best;
      chosen[id] = best;
      bounds[id] = emaRangeBounds(v, id, best, cap);
      if (emaFeasible(v,
                      nodes,
                      nn,
                      edges,
                      i + 1,
                      chosen,
                      bounds,
                      lower,
                      upper,
                      false)) {
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      return;
    }
  }
  for (int i = 0; i < nn; ++i) {
    v.x[nodes[i]] = lower[nodes[i]];
  }
  active[c] = 1;
  atomicAdd(counters + 4, 1ULL);
}
__global__ void emaSelect(View v,
                          const I* before,
                          JointComponents components,
                          int count,
                          const int* active,
                          unsigned long long* counters)
{
  const int c = blockIdx.x * blockDim.x + threadIdx.x;
  if (c >= count || !active[c]) {
    return;
  }
  View old = v;
  old.x = const_cast<I*>(before);
  I delta = 0;
  bool valid = true;
  for (int k = components.offsets[c]; k < components.offsets[c + 1]; ++k) {
    const int node = components.nodes[k];
    delta += absolute(v.x[node]) - absolute(before[node]);
    valid &= v.x[node] >= v.nodes[node].low && v.x[node] <= v.nodes[node].up;
  }
  for (int k = components.edge_offsets[c]; k < components.edge_offsets[c + 1];
       ++k) {
    const int id = components.edges[k];
    valid &= gap(v, id, distance(v, id)) <= gap(old, id, distance(old, id));
  }
  if (valid && delta < 0) {
    atomicAdd(counters + 5, 1ULL);
    return;
  }
  for (int k = components.offsets[c]; k < components.offsets[c + 1]; ++k) {
    const int node = components.nodes[k];
    v.x[node] = before[node];
  }
}
}  // namespace post
