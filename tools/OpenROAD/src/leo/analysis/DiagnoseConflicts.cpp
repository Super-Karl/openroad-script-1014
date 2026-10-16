// SPDX-License-Identifier: BSD-3-Clause
// Read-only analysis: reproduce the graph and classify remaining violations.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "leo/Ema.h"
#include "leo/Graph.h"
#include "leo/Lpa.h"
#include "leo/Measurement.h"
#include "leo/Trim.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "utl/Logger.h"

namespace {
using Bounds = std::pair<int64_t, int64_t>;
int64_t gap(const leo::Arc& arc, int64_t distance)
{
  int64_t best = std::numeric_limits<int64_t>::max();
  for (const auto& r : arc.ranges) {
    int64_t d = 0;
    if (r.has_low) {
      d = std::max(d, r.low2 - distance);
    }
    if (r.has_high) {
      d = std::max(d, distance - r.high2);
    }
    best = std::min(best, d);
  }
  return best;
}
bool individuallyFeasible(const leo::Arc& arc, Bounds a, Bounds b)
{
  const int64_t low = b.first - a.second;
  const int64_t high = b.second - a.first;
  for (const auto& r : arc.ranges) {
    if ((!r.has_low || high >= r.low2) && (!r.has_high || low <= r.high2)) {
      return true;
    }
  }
  return false;
}
leo::LayerNodes build(odb::dbBlock* block,
                      odb::dbTechLayer* layer,
                      int k,
                      std::vector<Bounds>& full)
{
  const bool horizontal
      = layer->getDirection() == odb::dbTechLayerDir::HORIZONTAL;
  const int width = layer->getWidth();
  std::vector<leo::Trim> trims;
  std::vector<odb::Rect> obstacles;
  for (auto* net : block->getNets()) {
    if (net->getWire() == nullptr) {
      continue;
    }
    const bool supply = net->getSigType().isSupply();
    std::vector<odb::Rect> metal;
    auto add = [&](const odb::dbShape& shape) {
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
          add(box);
        }
      } else {
        add(shape);
      }
    }
    for (const auto& trim : leo::makeTrimBoxes(
             metal, width, horizontal, layer->getName(), net->getId())) {
      trims.push_back(trim);
    }
  }
  leo::LayerNodes graph;
  graph.layer = layer->getName();
  graph.horizontal = horizontal;
  graph.pitch = layer->getPitch();
  graph.dbu = block->getDbUnitsPerMicron();
  graph.rules = leo::makeLayerRules(width, k);
  graph.nodes = leo::makeVnodes(trims, 0, trims.size(), horizontal);
  leo::computeDomains(graph, obstacles, true);
  for (const auto& n : graph.nodes) {
    full.emplace_back(n.low2, n.up2);
  }
  leo::pruneDomains(graph, k);
  leo::buildArcs(graph);
  leo::collectComponents(graph);
  return graph;
}
}  // namespace
int main(int argc, char** argv)
{
  if (argc < 5) {
    std::cerr << "usage: diagnose_conflicts input.odb summary.csv details.csv "
                 "k [k...]\n";
    return 2;
  }
  try {
    utl::Logger logger;
    std::unique_ptr<odb::dbDatabase, decltype(&odb::dbDatabase::destroy)> db(
        odb::dbDatabase::create(), odb::dbDatabase::destroy);
    db->setLogger(&logger);
    std::ifstream input(argv[1], std::ios::binary);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    db->read(input);
    auto* block = db->getChip()->getBlock();
    std::ofstream summary(argv[2]), details(argv[3]);
    summary.exceptions(std::ios::failbit | std::ios::badbit);
    details.exceptions(std::ios::failbit | std::ios::badbit);
    summary << std::setprecision(12);
    details << std::setprecision(12);
    summary << "k,layer,nodes,arcs,initial_violations,violations,remaining_5nm,"
               "extension_um,lpa_extension_um,domain_impossible,full_domain_"
               "impossible,pruning_only,individually_feasible,selected_"
               "violations\n";
    details << "k,layer,arc,type,src,dst,gap_nm,pruned_individually_feasible,"
               "full_individually_feasible,pruned_delta_low_nm,pruned_delta_"
               "high_nm,full_delta_low_nm,full_delta_high_nm\n";
    std::ofstream components;
    if (const char* path = std::getenv("LEO_DIAGNOSTIC_COMPONENTS")) {
      components.open(path);
      components.exceptions(std::ios::failbit | std::ios::badbit);
    }
    std::map<std::string, std::map<int, int64_t>> candidate_positions;
    std::ofstream candidate_report;
    if (const char* path = std::getenv("LEO_DIAGNOSTIC_CANDIDATE")) {
      std::ifstream positions(path);
      positions.exceptions(std::ios::badbit);
      if (!positions) {
        throw std::runtime_error("Cannot read candidate positions");
      }
      std::string line;
      std::getline(positions, line);
      while (std::getline(positions, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream fields(line);
        std::string layer;
        int id;
        int64_t value;
        if (!(fields >> layer >> id >> value)) {
          throw std::runtime_error("Invalid candidate row");
        }
        candidate_positions[layer].emplace(id, value);
      }
      candidate_report.open(std::string(path) + ".checked.csv");
      candidate_report.exceptions(std::ios::failbit | std::ios::badbit);
      candidate_report
          << std::setprecision(12)
          << "layer,violations,remaining_5nm,A,B,C,extension_um,expanded_"
             "window_violations,newly_violated_legal_arcs\n";
    }
    for (int argument = 4; argument < argc; ++argument) {
      const int k = std::stoi(argv[argument]);
      size_t total = 0, hard_total = 0, pruning_total = 0, combo_total = 0,
             count_total = 0, arcs_total = 0, initial_total = 0,
             remaining_total = 0;
      double extension_total = 0;
      size_t candidate_violations = 0, candidate_remaining = 0;
      std::array<size_t, 3> candidate_types{};
      double candidate_extension = 0;
      int64_t lpa_total2 = 0;
      for (const char* name : {"M2", "M3", "M4", "M5", "M6", "M7"}) {
        auto* layer = block->getTech()->findLayer(name);
        if (layer == nullptr) {
          continue;
        }
        std::vector<Bounds> full;
        auto graph = build(block, layer, k, full);
        leo::solveLpa(graph);
        const auto lpa = leo::assessLayer(graph, graph.dbu);
        leo::solveEma(graph);
        const auto q = leo::assessLayer(graph, graph.dbu);
        if (q.selected_violations != 0) {
          throw std::runtime_error("accepted range violation");
        }
        if (candidate_report.is_open() && k == 3) {
          auto check = graph;
          for (const auto& [id, value] : candidate_positions[name]) {
            auto& node = check.nodes.at(id);
            if (check.horizontal) {
              node.pos_x2 = value;
            } else {
              node.pos_y2 = value;
            }
          }
          const auto changed = leo::assessLayer(check, check.dbu);
          size_t newly_violated = 0;
          for (const auto& arc : graph.arcs) {
            auto distance = [&](const leo::LayerNodes& model) {
              return leo::posAlong2(model.nodes[arc.dst], model.horizontal)
                     - leo::posAlong2(model.nodes[arc.src], model.horizontal);
            };
            newly_violated += gap(arc, distance(graph)) == 0
                              && gap(arc, distance(check)) > 0;
          }
          // Independently widen all domains to +/-P and regenerate potential
          // pairs. This checks that pruning did not hide any final violation.
          auto expanded = check;
          const int64_t p = k * graph.rules.dt;
          expanded.rules.move_limit = p;
          for (auto& n : expanded.nodes) {
            const auto initial = leo::along2(n, expanded.horizontal);
            n.low2 = initial - 2 * p;
            n.up2 = initial + 2 * p;
          }
          leo::buildArcs(expanded);
          const auto all_pairs = leo::assessLayer(expanded, expanded.dbu);
          if (all_pairs.violations != changed.violations
              || all_pairs.by_type != changed.by_type
              || all_pairs.remaining != changed.remaining
              || newly_violated != 0) {
            throw std::runtime_error(
                "Independent candidate verification failed");
          }
          candidate_report << name << ',' << changed.violations << ','
                           << changed.remaining;
          for (size_t t = 0; t < 3; ++t) {
            candidate_report << ',' << changed.by_type[t];
            candidate_types[t] += changed.by_type[t];
          }
          candidate_report << ',' << changed.extension_dbu / check.dbu << ','
                           << all_pairs.violations << ',' << newly_violated
                           << '\n';
          candidate_violations += changed.violations;
          candidate_remaining += changed.remaining;
          candidate_extension += changed.extension_dbu / check.dbu;
        }
        if (components.is_open() && k == 3) {
          for (const auto& component : graph.components) {
            bool unresolved = false;
            for (int id : component.arcs) {
              unresolved |= graph.arcs[id].selected_range < 0;
            }
            if (!unresolved) {
              continue;
            }
            components << "{\"layer\":" << std::quoted(graph.layer)
                       << ",\"component\":" << component.id
                       << ",\"dbu\":" << graph.dbu << ",\"nodes\":[";
            bool first = true;
            for (int id : component.nodes) {
              if (!first) {
                components << ',';
              }
              first = false;
              const auto& n = graph.nodes[id];
              components << "{\"id\":" << id << ",\"low\":" << n.low2
                         << ",\"up\":" << n.up2
                         << ",\"weight\":" << (n.high_end ? 1 : -1)
                         << ",\"initial\":" << leo::along2(n, graph.horizontal)
                         << ",\"position\":"
                         << leo::posAlong2(n, graph.horizontal) << '}';
            }
            components << "],\"arcs\":[";
            first = true;
            for (int id : component.arcs) {
              if (!first) {
                components << ',';
              }
              first = false;
              const auto& arc = graph.arcs[id];
              components << "{\"id\":" << id << ",\"src\":" << arc.src
                         << ",\"dst\":" << arc.dst
                         << ",\"type\":" << std::quoted(leo::toString(arc.type))
                         << ",\"selected\":" << arc.selected_range
                         << ",\"ranges\":[";
              bool first_range = true;
              for (const auto& range : arc.ranges) {
                if (!first_range) {
                  components << ',';
                }
                first_range = false;
                components << '[';
                if (range.has_low) {
                  components << range.low2;
                } else {
                  components << "null";
                }
                components << ',';
                if (range.has_high) {
                  components << range.high2;
                } else {
                  components << "null";
                }
                components << ']';
              }
              components << "]}";
            }
            components << "]}\n";
          }
        }
        size_t impossible = 0, hard = 0, pruning = 0, combo = 0;
        for (const auto& arc : graph.arcs) {
          const auto& a = graph.nodes[arc.src];
          const auto& b = graph.nodes[arc.dst];
          const auto d = leo::posAlong2(b, graph.horizontal)
                         - leo::posAlong2(a, graph.horizontal);
          const auto residual = gap(arc, d);
          if (residual == 0) {
            continue;
          }
          const bool pruned_ok
              = individuallyFeasible(arc, {a.low2, a.up2}, {b.low2, b.up2});
          const bool full_ok
              = individuallyFeasible(arc, full[arc.src], full[arc.dst]);
          impossible += !pruned_ok;
          hard += !full_ok;
          pruning += !pruned_ok && full_ok;
          combo += pruned_ok;
          const double nm = 500.0 / graph.dbu;
          details << k << ',' << name << ',' << arc.id << ','
                  << leo::toString(arc.type) << ',' << arc.src << ',' << arc.dst
                  << ',' << residual * nm << ',' << pruned_ok << ',' << full_ok
                  << ',' << (b.low2 - a.up2) * nm << ','
                  << (b.up2 - a.low2) * nm << ','
                  << (full[arc.dst].first - full[arc.src].second) * nm << ','
                  << (full[arc.dst].second - full[arc.src].first) * nm << '\n';
        }
        if (hard + pruning + combo != q.violations
            || impossible != hard + pruning) {
          throw std::runtime_error("classification mismatch");
        }
        summary << k << ',' << name << ',' << q.nodes << ',' << q.arcs << ','
                << q.initial_violations << ',' << q.violations << ','
                << q.remaining << ',' << q.extension_dbu / graph.dbu << ','
                << lpa.extension_dbu / graph.dbu << ',' << impossible << ','
                << hard << ',' << pruning << ',' << combo << ','
                << q.selected_violations << '\n';
        count_total += q.nodes;
        arcs_total += q.arcs;
        initial_total += q.initial_violations;
        remaining_total += q.remaining;
        extension_total += q.extension_dbu / graph.dbu;
        lpa_total2 += graph.lpa.added_length2;
        total += q.violations;
        hard_total += hard;
        pruning_total += pruning;
        combo_total += combo;
        std::cout << "k=" << k << " layer=" << name
                  << " violations=" << q.violations
                  << " full_domain_impossible=" << hard
                  << " pruning_only=" << pruning
                  << " individually_feasible=" << combo << std::endl;
      }
      summary << k << ",TOTAL," << count_total << ',' << arcs_total << ','
              << initial_total << ',' << total << ',' << remaining_total << ','
              << extension_total << ','
              << lpa_total2 / 2.0 / block->getDbUnitsPerMicron() << ','
              << hard_total + pruning_total << ',' << hard_total << ','
              << pruning_total << ',' << combo_total << ",0\n";
      std::cout << "TOTAL k=" << k << " violations=" << total
                << " hard=" << hard_total << " pruning=" << pruning_total
                << " combination=" << combo_total
                << " extension_um=" << extension_total << std::endl;
      if (candidate_report.is_open() && k == 3) {
        candidate_report << "TOTAL," << candidate_violations << ','
                         << candidate_remaining;
        for (size_t t : candidate_types) {
          candidate_report << ',' << t;
        }
        candidate_report << ',' << candidate_extension << ','
                         << candidate_violations << ",0\n";
        std::cout << "CANDIDATE violations=" << candidate_violations
                  << " extension_um=" << candidate_extension << std::endl;
      }
      if (k == 3
          && (total != 340 || count_total != 332664 || arcs_total != 372205
              || std::abs(extension_total - 2441.818) > 1e-6)) {
        throw std::runtime_error("does not reproduce archived k=3 baseline");
      }
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
