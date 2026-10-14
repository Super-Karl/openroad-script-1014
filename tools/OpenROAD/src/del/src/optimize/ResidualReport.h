// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#pragma once
#include <string>
#include <vector>

#include "del/Graph.h"
#include "del/Optimizer.h"
namespace del {
// Export exact positive continuous residuals, without treating a tolerance as
// legalization. Pair feasibility is necessary, not sufficient, for a graph.
// The default table annotates every positive residual with the 5 nm policy;
// only_unrepaired writes a separate table containing residuals >= 5 nm.
std::string writeResidualReport(const std::string& iteration_report,
                                const std::vector<LayerNodes>& layers,
                                const std::vector<LayerOptimizer>& optimizers,
                                double dbu_per_micron,
                                bool only_unrepaired = false);
// Export endpoint displacements for wire-extension estimates. The database
// wires remain unchanged; these are continuous overlay coordinates.
std::string writeDisplacementReport(
    const std::string& iteration_report,
    const std::vector<LayerNodes>& layers,
    const std::vector<LayerOptimizer>& optimizers,
    double dbu_per_micron);
}  // namespace del
