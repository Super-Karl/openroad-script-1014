// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>
#include <vector>

namespace delgpu {
struct PostNode
{
  int64_t origin2, low, up;
};
struct PostRange
{
  int64_t low2, high2;
  bool has_low, has_high;
};
// Exact geometry from the original graph, never reconstructed from floats.
// Topology, adjacency and components are reused from TrimOptimizer.
struct PostprocessGraph
{
  std::vector<PostNode> nodes;
  std::vector<int64_t> initial2;
  std::vector<PostRange> ranges;
  std::vector<int> color_nodes, color_offsets;
  int64_t tolerance2 = 0;
};
struct PostprocessOptions
{
  int max_sweeps = 24;
  int pair_rounds = 8;
  int retract_sweeps = 8;
  bool cooperative_retract = true;
  int propagation_rounds = 128;
};
struct PostprocessStats
{
  int64_t violations[3]{}, remaining[3]{};
  int64_t gap2 = 0, max_gap2 = 0, extent = 0;
};
struct PostprocessResult
{
  PostprocessStats quantized, repaired, retracted;
  int sweeps = 0, pair_rounds = 0, retract_sweeps = 0;
  int64_t single_moves = 0, pair_moves = 0, retract_moves = 0;
  int64_t cooperative_moves = 0, group_moves = 0;
  int propagation_rounds = 0;
  double quantize_ms = 0, repair_ms = 0, retract_ms = 0, total_ms = 0;
};
}  // namespace delgpu
