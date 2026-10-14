// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "del/Optimizer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace del {
namespace {
constexpr double kAlpha = 0.01;
constexpr double kEpsilon = 0.01;

Potential bump(double d, double a, double b)
{
  if (d <= a || d >= b) {
    return {};
  }
  const double t = (d - a) / (b - a);
  return {16 * t * t * (1 - t) * (1 - t),
          32 * t * (1 - t) * (1 - 2 * t) / (b - a)};
}

Potential lowerBound(double d, double bound)
{
  const double gap = std::min(0.0, d - bound);
  return {gap * gap, 2 * gap};
}
}  // namespace

Potential rulePotential(ArcType type, double d, double threshold)
{
  switch (type) {
    case ArcType::kA:
      return lowerBound(d, threshold);
    case ArcType::kB:
      return d < 0 ? lowerBound(d, 0) : bump(d, 0, threshold);
    case ArcType::kC:
      return d < 0 ? bump(d, -threshold, 0) : bump(d, 0, threshold);
  }
  throw std::invalid_argument("Unknown DEL arc type");
}

LayerOptimizer::LayerOptimizer(const LayerNodes& layer,
                               double repair_tolerance_dbu,
                               double dbu_per_micron,
                               double jump_offset_nm,
                               double max_step_w)
    : layer_(layer),
      width_(layer.rules.width),
      repair_tolerance_dbu_(repair_tolerance_dbu),
      jump_offset_(jump_offset_nm * dbu_per_micron / (1000.0 * width_)),
      max_step_(max_step_w)
{
  if (width_ <= 0 || !std::isfinite(dbu_per_micron) || dbu_per_micron <= 0) {
    throw std::invalid_argument(
        "DEL optimization requires positive layer width and DBU scale");
  }
  if (!std::isfinite(jump_offset_nm) || jump_offset_nm <= 0
      || !std::isfinite(jump_offset_) || jump_offset_ <= 0) {
    throw std::invalid_argument(
        "DEL barrier jump offset must be finite and positive");
  }
  if (!std::isfinite(max_step_) || max_step_ <= 0) {
    throw std::invalid_argument("DEL max_step must be finite and positive (W)");
  }
  for (const auto& node : layer.nodes) {
    const auto origin = along2(node, layer.horizontal);
    positions_.push_back(0);
    low_.push_back(node.blocked ? 0 : (node.low2 - origin) / (2 * width_));
    up_.push_back(node.blocked ? 0 : (node.up2 - origin) / (2 * width_));
    if (low_.back() > 0 || up_.back() < 0) {
      throw std::invalid_argument("DEL initial position is outside its domain");
    }
  }
  degree_.resize(layer.nodes.size(), 1);
  incident_arcs_.resize(layer.nodes.size());
  stalled_rounds_.resize(layer.arcs.size(), 0);
  for (size_t index = 0; index < layer.arcs.size(); ++index) {
    const auto& arc = layer.arcs[index];
    incident_arcs_.at(arc.src).push_back(index);
    incident_arcs_.at(arc.dst).push_back(index);
    ++degree_.at(arc.src);
    ++degree_.at(arc.dst);
  }
}

OptimizationStats LayerOptimizer::evaluate(const std::vector<double>& positions,
                                           double lambda,
                                           std::vector<double>* gradient) const
{
  OptimizationStats stats;
  if (gradient != nullptr) {
    gradient->assign(positions.size(), 0);
  }
  for (size_t i = 0; i < positions.size(); ++i) {
    const double x = positions[i];
    const double smooth = std::hypot(x, kEpsilon);
    stats.energy += kAlpha * (smooth - kEpsilon);
    stats.extent_dbu += std::abs(x) * width_;
    if (gradient != nullptr) {
      (*gradient)[i] = kAlpha * x / smooth;
    }
  }
  for (const auto& arc : layer_.arcs) {
    const double d = arcDistance(arc, positions);
    const auto& rules = layer_.rules;
    const double threshold = (arc.type == ArcType::kA   ? rules.d
                              : arc.type == ArcType::kB ? rules.dt
                                                        : rules.du)
                             / width_;
    const auto potential = rulePotential(arc.type, d, threshold);
    stats.rule_energy += potential.value;
    if (gradient != nullptr) {
      (*gradient)[arc.src] -= lambda * potential.derivative;
      (*gradient)[arc.dst] += lambda * potential.derivative;
    }
    double gap = std::numeric_limits<double>::infinity();
    for (const auto& range : arc.ranges) {
      double violation = 0;
      if (range.has_low) {
        violation = std::max(violation, range.low2 / (2 * width_) - d);
      }
      if (range.has_high) {
        violation = std::max(violation, d - range.high2 / (2 * width_));
      }
      gap = std::min(gap, violation);
    }
    if (gap > 0) {
      ++stats.violations[static_cast<size_t>(arc.type)];
      if (gap * width_ >= repair_tolerance_dbu_) {
        ++stats.remaining_violations[static_cast<size_t>(arc.type)];
      }
      stats.max_violation_dbu = std::max(stats.max_violation_dbu, gap * width_);
    }
  }
  stats.energy += lambda * stats.rule_energy;
  if (!std::isfinite(stats.energy)) {
    throw std::runtime_error("DEL encountered non-finite optimization energy");
  }
  return stats;
}

OptimizationStats LayerOptimizer::evaluate(double lambda) const
{
  return evaluate(positions_, lambda, nullptr);
}

OptimizationStats LayerOptimizer::gradientStep(double lambda)
{
  std::vector<double> gradient;
  const auto before = evaluate(positions_, lambda, &gradient);
  std::vector<double> direction(positions_.size());
  double largest = 0;
  for (size_t i = 0; i < direction.size(); ++i) {
    direction[i] = std::clamp(-gradient[i] / degree_[i],
                              low_[i] - positions_[i],
                              up_[i] - positions_[i]);
    largest = std::max(largest, std::abs(direction[i]));
  }
  const double scale = largest > max_step_ ? max_step_ / largest : 1;
  std::vector<double> candidate(positions_.size());
  // Try gradient descent first, then a deterministic perturbation to escape
  // stationary forbidden-interval midpoints. Neither may increase the energy.
  for (int attempt = 0; attempt < 3; ++attempt) {
    for (int backtrack = 0; backtrack < 20; ++backtrack) {
      const double step_size = std::ldexp(scale, -backtrack);
      double max_step = 0;
      size_t moved = 0;
      for (size_t i = 0; i < candidate.size(); ++i) {
        const double jitter
            = attempt == 0 ? 0 : (((i + attempt) % 2 == 0) ? 0.01 : -0.01);
        candidate[i] = std::clamp(
            positions_[i]
                + std::clamp(
                    step_size * (direction[i] + jitter), -max_step_, max_step_),
            low_[i],
            up_[i]);
        const double change = std::abs(candidate[i] - positions_[i]);
        max_step = std::max(max_step, change);
        moved += change > 1e-12;
      }
      if (moved == 0) {
        break;
      }
      auto after = evaluate(candidate, lambda, nullptr);
      if (after.energy < before.energy - 1e-12 * std::max(1.0, before.energy)) {
        positions_.swap(candidate);
        after.max_step_dbu = max_step * width_;
        after.moved = moved;
        after.accepted = true;
        return after;
      }
    }
    if (before.rule_energy == 0) {
      break;
    }
  }
  return before;
}

double LayerOptimizer::arcDistance(const Arc& arc,
                                   const std::vector<double>& positions) const
{
  const double initial = (along2(layer_.nodes[arc.dst], layer_.horizontal)
                          - along2(layer_.nodes[arc.src], layer_.horizontal))
                         / (2 * width_);
  return initial + positions[arc.dst] - positions[arc.src];
}

OptimizationStats LayerOptimizer::step(double lambda)
{
  const auto previous = positions_;
  auto stats = gradientStep(lambda);
  repairStalled(previous, stats);
  if (stats.stall_repaired > 0) {
    const auto repaired = evaluate(lambda);
    stats.energy = repaired.energy;
    stats.rule_energy = repaired.rule_energy;
    stats.extent_dbu = repaired.extent_dbu;
    stats.max_violation_dbu = repaired.max_violation_dbu;
    stats.violations = repaired.violations;
    stats.remaining_violations = repaired.remaining_violations;
    stats.accepted = true;
  }
  stats.moved = 0;
  stats.max_step_dbu = 0;
  for (size_t i = 0; i < positions_.size(); ++i) {
    const double distance = std::abs(positions_[i] - previous[i]);
    stats.moved += distance > 1e-12;
    stats.max_step_dbu = std::max(stats.max_step_dbu, distance * width_);
  }
  return stats;
}

std::vector<double> LayerOptimizer::displacementDbu() const
{
  std::vector<double> result = positions_;
  for (auto& value : result) {
    value *= width_;
  }
  return result;
}
}  // namespace del
