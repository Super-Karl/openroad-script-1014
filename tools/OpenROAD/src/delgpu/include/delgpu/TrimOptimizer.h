// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "delgpu/Parameters.h"
#include "delgpu/Postprocess.h"

namespace delgpu {
struct OptNode
{
  double low, up;
  int begin, end;
};
struct OptEdge
{
  int src, dst, type, range_begin, range_end;
  double initial, threshold;
};
struct OptRange
{
  double low, high;
  int has_low, has_high;
};
struct TrimStats
{
  double energy = 0, rule_energy = 0, extent_dbu = 0;
  double max_violation_dbu = 0, max_step_dbu = 0;
  unsigned long long violations[3]{}, remaining[3]{};
  unsigned long long moved = 0, repaired = 0;
  int candidates = 0;
  bool accepted = false;
  bool gradient_accepted = false;
};
// Numeric arrays only; OpenDB and GUI stay in the host adapter.
class TrimOptimizer
{
 public:
  TrimOptimizer(const std::vector<OptNode>& nodes,
                const std::vector<OptEdge>& edges,
                const std::vector<OptRange>& ranges,
                const std::vector<int>& incident,
                const std::vector<int>& component_edges,
                const std::vector<int>& component_offsets,
                const std::vector<int>& edge_components,
                double width,
                double tolerance_dbu,
                double max_step,
                double jump_offset_w,
                int max_backtracks,
                double learning_rate,
                double wire_weight = kDefaultWireWeight);
  ~TrimOptimizer();
  TrimOptimizer(const TrimOptimizer&) = delete;
  TrimOptimizer& operator=(const TrimOptimizer&) = delete;
  TrimStats evaluate(double lambda);
  TrimStats step(double lambda);
  TrimStats step(double lambda, double learning_rate);
  PostprocessResult postprocess(const PostprocessGraph& graph,
                                const PostprocessOptions& options = {});
  // Warm start for independent same-input experiments; validates hard domains.
  void setDisplacementDbu(const std::vector<double>& values);
  std::vector<double> displacementDbu();
  size_t deviceBytes() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace delgpu
