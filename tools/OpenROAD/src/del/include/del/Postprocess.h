// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "del/Graph.h"

namespace del {
struct PostprocessProgress
{
  std::atomic<size_t> completed{0};
  std::atomic<size_t> total{0};
  std::atomic<bool> cancelled{false};
};
struct PostprocessResult
{
  std::vector<double> polished;
  std::vector<double> legalized;
  size_t components = 0;
  size_t nonoptimal_components = 0;
  bool polish_optimal = false;
  double polish_worker_seconds = 0;
  double legalize_worker_seconds = 0;
};

// All coordinates are displacements in DBU on the original, immutable graph.
// The final solution uses integer DBU; residuals use the original arc ranges.
double trimResidualDbu(const LayerNodes& layer,
                       const Arc& arc,
                       const std::vector<double>& displacement);
PostprocessResult postprocessLayer(const LayerNodes& layer,
                                   const std::vector<double>& displacement,
                                   PostprocessProgress* progress = nullptr);
void writePostprocessReports(const std::string& report,
                             const std::vector<LayerNodes>& layers,
                             const std::vector<std::vector<double>>& before,
                             const std::vector<PostprocessResult>& results,
                             double dbu_per_micron);
}  // namespace del
