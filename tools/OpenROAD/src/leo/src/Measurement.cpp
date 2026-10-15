// SPDX-License-Identifier: BSD-3-Clause
#include "leo/Measurement.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>

#include "leo/Ema.h"
#include "leo/Leo.h"
#include "leo/Lpa.h"
#include "odb/db.h"
#include "utl/Logger.h"

namespace leo {
namespace {
int64_t intervalGap(const ArcRange& range, int64_t distance)
{
  int64_t gap = 0;
  if (range.has_low) {
    gap = std::max(gap, range.low2 - distance);
  }
  if (range.has_high) {
    gap = std::max(gap, distance - range.high2);
  }
  return gap;
}
int64_t arcGap(const Arc& arc, int64_t distance)
{
  if (arc.ranges.empty()) {
    throw std::runtime_error("LEO arc has no legal intervals");
  }
  int64_t gap = std::numeric_limits<int64_t>::max();
  for (const auto& range : arc.ranges) {
    gap = std::min(gap, intervalGap(range, distance));
  }
  return gap;
}
void add(GraphQuality& total, const GraphQuality& q)
{
  total.nodes += q.nodes;
  total.arcs += q.arcs;
  total.initial_violations += q.initial_violations;
  total.initial_remaining += q.initial_remaining;
  total.violations += q.violations;
  total.remaining += q.remaining;
  total.unselected += q.unselected;
  total.selected_violations += q.selected_violations;
  total.extent_dbu += q.extent_dbu;
  total.extension_dbu += q.extension_dbu;
  total.max_gap2 = std::max(total.max_gap2, q.max_gap2);
  for (int t = 0; t < 3; ++t) {
    total.by_type[t] += q.by_type[t];
    total.remaining_by_type[t] += q.remaining_by_type[t];
  }
}
double scale(odb::dbBlock* block)
{
  if (block == nullptr || block->getDbUnitsPerMicron() <= 0) {
    throw std::runtime_error("LEO quality: no design with a valid DBU scale");
  }
  return block->getDbUnitsPerMicron();
}
class Reporter
{
 public:
  Reporter(const std::string& path, double dbu, utl::Logger* logger)
      : dbu_(dbu), logger_(logger)
  {
    if (!path.empty()) {
      csv_.open(path);
      if (!csv_) {
        throw std::runtime_error("Cannot open LEO quality report: " + path);
      }
      csv_ << "stage,layer,nodes,arcs,initial_violations,initial_remaining_5nm,"
              "violations,A_violations,B_violations,C_violations,below_5nm,"
              "remaining_5nm,A_remaining_5nm,B_remaining_5nm,C_remaining_5nm,"
              "unselected_arcs,selected_range_violations,extent_um,wire_"
              "extension_um,max_gap_um\n"
           << std::setprecision(12);
    }
  }
  void write(const char* stage, const std::string& layer, const GraphQuality& q)
  {
    logger_->report(
        "LEO quality: stage={} layer={} nodes={} arcs={} initial_violations={} "
        "violations={} A={} B={} C={} below_5nm={} remaining_5nm={} "
        "unselected_arcs={} selected_range_violations={} extent_um={:.6f} "
        "wire_extension_um={:.6f} max_gap_um={:.6f}",
        stage,
        layer,
        q.nodes,
        q.arcs,
        q.initial_violations,
        q.violations,
        q.by_type[0],
        q.by_type[1],
        q.by_type[2],
        q.violations - q.remaining,
        q.remaining,
        q.unselected,
        q.selected_violations,
        q.extent_dbu / dbu_,
        q.extension_dbu / dbu_,
        q.max_gap2 / (2 * dbu_));
    if (csv_.is_open()) {
      csv_ << stage << ',' << layer << ',' << q.nodes << ',' << q.arcs << ','
           << q.initial_violations << ',' << q.initial_remaining << ','
           << q.violations;
      for (auto count : q.by_type) {
        csv_ << ',' << count;
      }
      csv_ << ',' << q.violations - q.remaining << ',' << q.remaining;
      for (auto count : q.remaining_by_type) {
        csv_ << ',' << count;
      }
      csv_ << ',' << q.unselected << ',' << q.selected_violations << ','
           << q.extent_dbu / dbu_ << ',' << q.extension_dbu / dbu_ << ','
           << q.max_gap2 / (2 * dbu_) << '\n';
      if (!csv_) {
        throw std::runtime_error("Failed to write LEO quality report");
      }
    }
  }

 private:
  double dbu_;
  utl::Logger* logger_;
  std::ofstream csv_;
};
}  // namespace

GraphQuality assessLayer(const LayerNodes& layer,
                         double dbu,
                         bool final_positions)
{
  if (!std::isfinite(dbu) || dbu <= 0) {
    throw std::invalid_argument("Invalid LEO DBU scale");
  }
  const double tolerance2 = 10.0 * dbu / 1000.0;
  GraphQuality q;
  q.nodes = layer.nodes.size();
  q.arcs = layer.arcs.size();
  for (const auto& node : layer.nodes) {
    const int64_t original = along2(node, layer.horizontal);
    const int64_t position
        = final_positions ? posAlong2(node, layer.horizontal) : original;
    if (position < node.low2 || position > node.up2
        || (node.blocked && position != original)) {
      throw std::runtime_error("LEO position violates a hard domain");
    }
    const double displacement = (position - original) / 2.0;
    q.extent_dbu += std::abs(displacement);
    q.extension_dbu += node.high_end ? displacement : -displacement;
  }
  for (const auto& arc : layer.arcs) {
    const auto& src = layer.nodes.at(arc.src);
    const auto& dst = layer.nodes.at(arc.dst);
    const int64_t initial_distance
        = along2(dst, layer.horizontal) - along2(src, layer.horizontal);
    const int64_t initial = arcGap(arc, initial_distance);
    q.initial_violations += initial > 0;
    q.initial_remaining += initial > 0 && initial >= tolerance2;
    const int64_t distance = final_positions
                                 ? posAlong2(dst, layer.horizontal)
                                       - posAlong2(src, layer.horizontal)
                                 : initial_distance;
    const int64_t gap = arcGap(arc, distance);
    q.max_gap2 = std::max(q.max_gap2, gap);
    if (gap > 0) {
      ++q.violations;
      ++q.by_type.at(static_cast<size_t>(arc.type));
      if (gap >= tolerance2) {
        ++q.remaining;
        ++q.remaining_by_type.at(static_cast<size_t>(arc.type));
      }
    }
    if (arc.selected_range < 0) {
      ++q.unselected;
    } else if (intervalGap(arc.ranges.at(arc.selected_range), distance) > 0) {
      ++q.selected_violations;
    }
  }
  return q;
}

void reportQuality(const Leo& tool,
                   odb::dbBlock* block,
                   const std::string& report,
                   utl::Logger* logger)
{
  const double dbu = scale(block);
  Reporter reporter(report, dbu, logger);
  GraphQuality total;
  for (const auto& layer : tool.getLayerNodes()) {
    const auto q = assessLayer(layer, dbu);
    if (std::abs(q.extension_dbu - layer.ema.length_after2 / 2.0) > 1e-6) {
      throw std::runtime_error(
          "LEO measured extension differs from EMA objective");
    }
    reporter.write("final", layer.layer, q);
    add(total, q);
  }
  reporter.write("final", "TOTAL", total);
}

void auditStages(const Leo& tool,
                 odb::dbBlock* block,
                 const std::string& report,
                 utl::Logger* logger)
{
  const double dbu = scale(block);
  Reporter reporter(report, dbu, logger);
  std::array<GraphQuality, 3> totals{};
  for (auto layer : tool.getLayerNodes()) {
    for (auto& node : layer.nodes) {
      node.pos_x2 = node.center_x2;
      node.pos_y2 = node.center_y2;
    }
    for (auto& arc : layer.arcs) {
      arc.selected_range = -1;
    }
    layer.lpa = {};
    layer.ema = {};
    auto q = assessLayer(layer, dbu);
    reporter.write("initial", layer.layer, q);
    add(totals[0], q);
    solveLpa(layer);
    q = assessLayer(layer, dbu);
    reporter.write("lpa", layer.layer, q);
    add(totals[1], q);
    solveEma(layer);
    q = assessLayer(layer, dbu);
    reporter.write("ema", layer.layer, q);
    add(totals[2], q);
  }
  for (int i = 0; i < 3; ++i) {
    reporter.write(i == 0   ? "initial"
                   : i == 1 ? "lpa"
                            : "ema",
                   "TOTAL",
                   totals[i]);
  }
}
}  // namespace leo
