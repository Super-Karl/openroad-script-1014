// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include "ResidualReport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

namespace del {
namespace {
struct Residual
{
  double gap = std::numeric_limits<double>::infinity();
  double domain_floor2 = std::numeric_limits<double>::infinity();
  double reachable_gap = std::numeric_limits<double>::infinity();
  const char* nearest = "none";
  bool nearest_reachable = false;
  bool pair_feasible = false;
};
Residual measure(const LayerNodes& layer, const Arc& arc, double d)
{
  Residual result;
  const auto& a = layer.nodes[arc.src];
  const auto& b = layer.nodes[arc.dst];
  const int64_t reachable_low2 = b.low2 - a.up2;
  const int64_t reachable_high2 = b.up2 - a.low2;
  const double scale = 2.0 * layer.rules.width;
  for (const auto& range : arc.ranges) {
    // Match the optimizer's normalized arithmetic and strict gap > 0 count.
    double gap = 0;
    if (range.has_low) {
      gap = std::max(gap, range.low2 / scale - d);
    }
    if (range.has_high) {
      gap = std::max(gap, d - range.high2 / scale);
    }
    const int64_t lo
        = range.has_low ? std::max(reachable_low2, range.low2) : reachable_low2;
    const int64_t hi = range.has_high ? std::min(reachable_high2, range.high2)
                                      : reachable_high2;
    if (gap < result.gap) {
      result.gap = gap;
      result.nearest = range.has_low && range.has_high && range.low2 == 0
                               && range.high2 == 0
                           ? "merge"
                       : !range.has_low ? "left_separate"
                                        : "right_separate";
      result.nearest_reachable = lo <= hi;
    }
    double floor2 = 0;
    if (range.has_low) {
      floor2
          = std::max(floor2, static_cast<double>(range.low2 - reachable_high2));
    }
    if (range.has_high) {
      floor2
          = std::max(floor2, static_cast<double>(reachable_low2 - range.high2));
    }
    result.domain_floor2 = std::min(result.domain_floor2, floor2);
    if (lo <= hi) {
      result.pair_feasible = true;
      result.reachable_gap
          = std::min(result.reachable_gap,
                     std::max({0.0, lo / scale - d, d - hi / scale}));
    }
  }
  return result;
}
void writeLayer(std::ofstream& out,
                const LayerNodes& layer,
                const LayerOptimizer& optimizer,
                double nm_per_dbu,
                bool only_unrepaired)
{
  const auto& positions = optimizer.positions();
  const double width = layer.rules.width;
  for (const auto& arc : layer.arcs) {
    const auto& a = layer.nodes[arc.src];
    const auto& b = layer.nodes[arc.dst];
    const double d = (along2(b, layer.horizontal) - along2(a, layer.horizontal))
                         / (2 * width)
                     + positions[arc.dst] - positions[arc.src];
    const auto r = measure(layer, arc, d);
    if (r.gap <= 0) {
      continue;
    }
    const double repair_distance_nm = r.gap * width * nm_per_dbu;
    // Compare in DBU, exactly as OptimizationStats does (no display rounding).
    const bool below_5nm = r.gap * width < kRepairToleranceNm / nm_per_dbu;
    if (only_unrepaired && below_5nm) {
      continue;
    }
    out << layer.layer << ',' << arc.id << ',' << toString(arc.type) << ','
        << arc.src << ',' << arc.dst << ',' << r.gap * width * nm_per_dbu << ','
        << d * width * nm_per_dbu << ',' << r.nearest << ','
        << r.nearest_reachable << ',' << r.pair_feasible << ','
        << r.domain_floor2 * 0.5 * nm_per_dbu << ',';
    if (r.pair_feasible) {
      out << r.reachable_gap * width * nm_per_dbu;
    }
    out << ',' << a.blocked << ',' << b.blocked << ',' << repair_distance_nm
        << ',' << below_5nm << ','
        << (below_5nm ? "treated_repaired" : "unrepaired");
    for (const auto* node : {&a, &b}) {
      const double shift = positions[node->id] * width;
      const double dx = layer.horizontal ? shift : 0;
      const double dy = layer.horizontal ? 0 : shift;
      const double um_per_dbu = nm_per_dbu / 1000.0;
      out << ',' << node->trim_index << ','
          << (node->center_x2 * 0.5 + dx) * um_per_dbu << ','
          << (node->center_y2 * 0.5 + dy) * um_per_dbu << ','
          << (node->trim.xMin() + dx) * um_per_dbu << ','
          << (node->trim.yMin() + dy) * um_per_dbu << ','
          << (node->trim.xMax() + dx) * um_per_dbu << ','
          << (node->trim.yMax() + dy) * um_per_dbu;
    }
    out << '\n';
  }
}
}  // namespace
std::string writeResidualReport(const std::string& iteration_report,
                                const std::vector<LayerNodes>& layers,
                                const std::vector<LayerOptimizer>& optimizers,
                                double dbu_per_micron,
                                bool only_unrepaired)
{
  std::filesystem::path path(iteration_report);
  path.replace_filename(
      path.stem().string()
      + (only_unrepaired ? "_unrepaired.csv" : "_residuals.csv"));
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Cannot open DEL residual report: "
                             + path.string());
  }
  out << std::setprecision(17)
      << "layer,arc_id,type,src,dst,residual_nm,distance_nm,nearest_region,"
         "nearest_region_reachable,pair_feasible,domain_residual_floor_nm,"
         "nearest_reachable_residual_nm,src_blocked,dst_blocked,repair_"
         "distance_nm,"
         "below_5nm,status,src_trim_index,src_x_um,src_y_um,src_xmin_um,"
         "src_ymin_um,src_xmax_um,src_ymax_um,dst_trim_index,dst_x_um,dst_y_um,"
         "dst_xmin_um,dst_ymin_um,dst_xmax_um,dst_ymax_um\n";
  for (size_t i = 0; i < layers.size(); ++i) {
    writeLayer(
        out, layers[i], optimizers[i], 1000 / dbu_per_micron, only_unrepaired);
  }
  out.close();
  if (!out) {
    throw std::runtime_error("Failed to write DEL residual report: "
                             + path.string());
  }
  return path.string();
}
std::string writeDisplacementReport(
    const std::string& iteration_report,
    const std::vector<LayerNodes>& layers,
    const std::vector<LayerOptimizer>& optimizers,
    double dbu_per_micron)
{
  std::filesystem::path path(iteration_report);
  path.replace_filename(path.stem().string() + "_displacements.csv");
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("Cannot open DEL displacement report: "
                             + path.string());
  }
  out << std::setprecision(17)
      << "layer,node_id,trim_index,net_id,high_end,blocked,initial_along_um,"
         "displacement_um,outward_extension_um,inward_retraction_um\n";
  for (size_t l = 0; l < layers.size(); ++l) {
    const auto& layer = layers[l];
    const auto displacements = optimizers[l].displacementDbu();
    for (size_t i = 0; i < layer.nodes.size(); ++i) {
      const auto& node = layer.nodes[i];
      const double displacement = displacements[i] / dbu_per_micron;
      const double outward = node.high_end ? displacement : -displacement;
      out << layer.layer << ',' << node.id << ',' << node.trim_index << ','
          << node.net_id << ',' << node.high_end << ',' << node.blocked << ','
          << along2(node, layer.horizontal) / (2.0 * dbu_per_micron) << ','
          << displacement << ',' << std::max(0.0, outward) << ','
          << std::max(0.0, -outward) << '\n';
    }
  }
  out.close();
  if (!out) {
    throw std::runtime_error("Failed to write DEL displacement report: "
                             + path.string());
  }
  return path.string();
}
}  // namespace del
