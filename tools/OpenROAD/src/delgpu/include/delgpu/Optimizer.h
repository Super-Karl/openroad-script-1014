// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once

#include <array>
#include <vector>

#include "delgpu/Graph.h"

namespace delgpu {
constexpr double kRepairToleranceNm = 5.0;
struct Potential
{
  double value = 0;
  double derivative = 0;
};

// Coordinates and distances are normalized by the layer width.
Potential rulePotential(ArcType type, double distance, double threshold);

struct OptimizationStats
{
  double energy = 0;
  double rule_energy = 0;
  double extent_dbu = 0;
  double max_violation_dbu = 0;
  double max_step_dbu = 0;
  std::array<size_t, 3> violations{};
  std::array<size_t, 3> remaining_violations{};
  size_t moved = 0;
  bool accepted = false;
  size_t stall_triggered = 0;
  size_t stall_repaired = 0;
  size_t stall_unresolved = 0;
  double max_repair_dbu = 0;
};

class LayerOptimizer
{
 public:
  explicit LayerOptimizer(const LayerNodes& layer,
                          double repair_tolerance_dbu = 0,
                          double dbu_per_micron = 1000,
                          double jump_offset_nm = 1.0,
                          double max_step_w = 0.2);
  OptimizationStats evaluate(double lambda) const;
  OptimizationStats step(double lambda);
  std::vector<double> displacementDbu() const;
  const std::vector<double>& positions() const { return positions_; }

 private:
  OptimizationStats gradientStep(double lambda);
  double arcDistance(const Arc& arc,
                     const std::vector<double>& positions) const;
  bool arcViolated(const Arc& arc, const std::vector<double>& positions) const;
  bool jumpAcrossBarrier(const Arc& arc, double& distance);
  void repairStalled(const std::vector<double>& previous,
                     OptimizationStats& stats);
  OptimizationStats evaluate(const std::vector<double>& positions,
                             double lambda,
                             std::vector<double>* gradient) const;
  const LayerNodes& layer_;
  double width_;
  double repair_tolerance_dbu_;
  double jump_offset_;
  double max_step_;
  std::vector<double> positions_;
  std::vector<double> low_;
  std::vector<double> up_;
  std::vector<int> degree_;
  std::vector<std::vector<size_t>> incident_arcs_;
  std::vector<int> stalled_rounds_;
};
}  // namespace delgpu
