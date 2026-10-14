// SPDX-License-Identifier: BSD-3-Clause
#include "BuildGraph.h"

#include <array>
#include <chrono>

#include "delgpu/GraphGpu.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "utl/Logger.h"

namespace delgpu {
LayerNodes buildLayerGraph(odb::dbBlock* block,
                           odb::dbTechLayer* layer,
                           size_t trim_offset,
                           bool gpu,
                           bool verify,
                           utl::Logger* logger)
{
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  const int width = layer->getWidth();
  const bool horizontal
      = layer->getDirection() == odb::dbTechLayerDir::HORIZONTAL;
  std::vector<odb::Rect> obstacles;
  std::vector<Trim> trims;
  for (auto* net : block->getNets()) {
    if (net->getWire() == nullptr) {
      continue;
    }
    const bool supply = net->getSigType().isSupply();
    std::vector<odb::Rect> metal;
    auto add_shape = [&](const odb::dbShape& shape) {
      if (shape.getTechLayer() != layer) {
        return;
      }
      obstacles.push_back(shape.getBox());
      if (!supply) {
        metal.push_back(shape.getBox());
      }
    };
    odb::dbWireShapeItr it;
    odb::dbShape shape;
    for (it.begin(net->getWire()); it.next(shape);) {
      if (shape.isVia()) {
        std::vector<odb::dbShape> boxes;
        odb::dbShape::getViaBoxes(shape, boxes);
        for (const auto& box : boxes) {
          add_shape(box);
        }
      } else {
        add_shape(shape);
      }
    }
    if (metal.empty()) {
      continue;
    }
    for (auto& trim : makeTrimBoxes(
             metal, width, horizontal, layer->getName(), net->getId())) {
      trims.push_back(std::move(trim));
    }
  }
  LayerNodes graph;
  graph.layer = layer->getName();
  graph.horizontal = horizontal;
  graph.pitch = layer->getPitch();
  graph.rules = makeLayerRules(width);
  graph.nodes = makeVnodes(trims, 0, trims.size(), horizontal);
  for (auto& node : graph.nodes) {
    node.trim_index += trim_offset;
  }
  const auto geometry_end = Clock::now();
  if (gpu) {
    buildGraphGpu(graph, obstacles);
  } else {
    computeDomains(graph, obstacles);
    buildArcs(graph);
  }
  const auto graph_end = Clock::now();
  if (verify) {
    LayerNodes reference = graph;
    reference.arcs.clear();
    computeDomains(reference, obstacles);
    buildArcs(reference);
    verifyGraph(graph, reference);
    logger->report(
        "DELGPU verify: layer={} nodes={} arcs={} CPU/GPU exact match",
        graph.layer,
        graph.nodes.size(),
        graph.arcs.size());
  }
  const auto verify_end = Clock::now();
  std::array<size_t, 3> counts{};
  size_t blocked = 0;
  for (const auto& arc : graph.arcs) {
    ++counts[static_cast<size_t>(arc.type)];
  }
  for (const auto& node : graph.nodes) {
    blocked += node.blocked;
  }
  auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  logger->report(
      "DELGPU graph: layer={} trims={} nodes={} obstacles={} blocked={} A={} "
      "B={} C={} arcs={}",
      graph.layer,
      trims.size(),
      graph.nodes.size(),
      obstacles.size(),
      blocked,
      counts[0],
      counts[1],
      counts[2],
      graph.arcs.size());
  logger->report(
      "DELGPU timing (ms): layer={} geometry_cpu={:.3f} domains_arcs_{}={:.3f} "
      "verify={:.3f}",
      graph.layer,
      ms(start, geometry_end),
      gpu ? "gpu" : "cpu",
      ms(geometry_end, graph_end),
      ms(graph_end, verify_end));
  return graph;
}
}  // namespace delgpu
