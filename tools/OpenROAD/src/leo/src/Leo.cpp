// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#include "leo/Leo.h"

#include <array>
#include <chrono>
#include <stdexcept>

#include "odb/db.h"
#include "utl/Logger.h"

namespace leo {

Leo::Leo(utl::Logger* logger) : logger_(logger)
{
}

void Leo::run(int prune_factor)
{
  if (prune_factor <= 0) {
    throw std::invalid_argument("LEO prune_factor must be a positive integer");
  }
  using Clock = std::chrono::steady_clock;
  const auto run_start = Clock::now();

  const auto milliseconds = [](Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
  };

  // Clear once per invocation; keep completed layers for the combined display.
  trims_.clear();
  layer_nodes_.clear();
  auto* block = init();
  double layers_total_ms = 0.0;
  int processed_layers = 0;
  if (block != nullptr) {
    constexpr std::array<const char*, 6> layers{
        "M2", "M3", "M4", "M5", "M6", "M7"};
    for (const char* name : layers) {
      auto* layer = block->getTech()->findLayer(name);
      if (layer == nullptr || layer->getRoutingLevel() == 0
          || layer->getWidth() == 0
          || (layer->getDirection() != odb::dbTechLayerDir::HORIZONTAL
              && layer->getDirection() != odb::dbTechLayerDir::VERTICAL)) {
        logger_->report(
            "LEO: skipping layer {} (missing or invalid routing layer)", name);
        continue;
      }

      logger_->report("LEO layer {}: begin", name);
      const auto layer_start = Clock::now();
      buildGraph(block, layer, prune_factor);
      const auto graph_end = Clock::now();
      runLpa(layer_nodes_.back());
      const auto lpa_end = Clock::now();
      runEma(layer_nodes_.back());
      const auto layer_end = Clock::now();

      const double layer_ms = milliseconds(layer_start, layer_end);
      layers_total_ms += layer_ms;
      ++processed_layers;
      logger_->report(
          "LEO timing (ms): layer={} buildGraph={:.3f} LPA={:.3f} "
          "EMA={:.3f} total={:.3f}",
          name,
          milliseconds(layer_start, graph_end),
          milliseconds(graph_end, lpa_end),
          milliseconds(lpa_end, layer_end),
          layer_ms);
    }
  }
  size_t node_total = 0;
  size_t blocked_total = 0;
  std::array<size_t, 3> arc_totals{};
  for (const auto& layer_nodes : layer_nodes_) {
    node_total += layer_nodes.nodes.size();
    for (const Arc& arc : layer_nodes.arcs) {
      ++arc_totals[static_cast<size_t>(arc.type)];
    }
    for (const Vnode& node : layer_nodes.nodes) {
      blocked_total += node.blocked ? 1 : 0;
    }
  }
  logger_->report("LEO vnodes: layers={} total={} trims_total={}",
                  layer_nodes_.size(),
                  node_total,
                  trims_.size());
  logger_->report(
      "LEO domains: layers={} blocked={}", layer_nodes_.size(), blocked_total);
  logger_->report("LEO arcs: layers={} A={} B={} C={} total={}",
                  layer_nodes_.size(),
                  arc_totals[static_cast<size_t>(ArcType::kA)],
                  arc_totals[static_cast<size_t>(ArcType::kB)],
                  arc_totals[static_cast<size_t>(ArcType::kC)],
                  arc_totals[0] + arc_totals[1] + arc_totals[2]);
  logger_->report("LEO total timing (ms): layers={} total={:.3f} run={:.3f}",
                  processed_layers,
                  layers_total_ms,
                  milliseconds(run_start, Clock::now()));
}

}  // namespace leo
