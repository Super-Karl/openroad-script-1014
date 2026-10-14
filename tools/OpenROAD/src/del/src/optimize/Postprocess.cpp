// SPDX-License-Identifier: BSD-3-Clause
#include "del/Postprocess.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>

#include "ortools/linear_solver/linear_solver.h"
#include "ortools/linear_solver/solve_mp_model.h"

namespace del {
namespace {
using operations_research::MPConstraint;
using operations_research::MPSolver;
using operations_research::MPVariable;
constexpr double inf = std::numeric_limits<double>::infinity();

double origin(const LayerNodes& layer, int id)
{
  return along2(layer.nodes[id], layer.horizontal) * 0.5;
}
std::pair<double, double> domain(const LayerNodes& layer, int id)
{
  const auto& n = layer.nodes[id];
  return n.blocked ? std::pair{0.0, 0.0}
                   : std::pair{n.low2 * 0.5 - origin(layer, id),
                               n.up2 * 0.5 - origin(layer, id)};
}
double extent(const std::vector<double>& x)
{
  double sum = 0;
  for (double v : x) {
    sum += std::abs(v);
  }
  return sum;
}
std::unique_ptr<MPSolver> solver(const char* backend)
{
  std::unique_ptr<MPSolver> result(MPSolver::CreateSolver(backend));
  if (!result) {
    throw std::runtime_error(std::string("DEL postprocess requires OR-Tools ")
                             + backend);
  }
  return result;
}

// OR-Tools 9.14's HiGHS MPSolver interface drops thread/time/output options.
// Its proto entry point forwards these options before constructing the model.
MPSolver::ResultStatus solve(MPSolver& model, double seconds)
{
  const auto type = model.ProblemType();
  if (type != MPSolver::HIGHS_LINEAR_PROGRAMMING
      && type != MPSolver::HIGHS_MIXED_INTEGER_PROGRAMMING) {
    model.set_time_limit(seconds * 1000);
    return model.Solve();
  }
  operations_research::MPModelRequest request;
  model.ExportModelToProto(request.mutable_model());
  request.set_solver_type(
      static_cast<operations_research::MPModelRequest::SolverType>(type));
  request.set_solver_time_limit_seconds(seconds);
  request.set_enable_internal_solver_output(false);
  request.set_solver_specific_parameters(
      "threads=1,output_flag=false,log_to_console=false,mip_rel_gap=0");
  const auto response = operations_research::SolveMPModel(std::move(request));
  const auto status = static_cast<MPSolver::ResultStatus>(response.status());
  if (status == MPSolver::OPTIMAL || status == MPSolver::FEASIBLE) {
    // Some backend versions report FEASIBLE without returning an incumbent.
    if (response.variable_value_size() != model.NumVariables()
        || !model.LoadSolutionFromProto(response).ok()) {
      return MPSolver::NOT_SOLVED;
    }
  }
  return status;
}

bool polish(const LayerNodes& layer,
            const std::vector<int>& nodes,
            const std::vector<int>& arcs,
            const std::vector<int>& local,
            const std::vector<double>& before,
            std::vector<double>& after)
{
  auto lp = MPSolver::SupportsProblemType(MPSolver::HIGHS_LINEAR_PROGRAMMING)
                ? solver("HIGHS_LINEAR_PROGRAMMING")
                : solver("GLOP");
  lp->SetNumThreads(1).IgnoreError();
  lp->set_time_limit(1000);
  std::vector<MPVariable*> x;
  for (int id : nodes) {
    const auto& n = layer.nodes[id];
    const auto [lo, hi] = domain(layer, id);
    x.push_back(lp->MakeNumVar(lo, hi, ""));
    lp->MutableObjective()->SetCoefficient(x.back(), n.high_end ? 1 : -1);
  }
  for (int aid : arcs) {
    const auto& a = layer.arcs[aid];
    const double initial = origin(layer, a.dst) - origin(layer, a.src);
    const double d = initial + before[a.dst] - before[a.src];
    double best = inf, low = 0, high = 0;
    for (const auto& r : a.ranges) {
      const double l = r.has_low ? r.low2 * 0.5 : -inf;
      const double u = r.has_high ? r.high2 * 0.5 : inf;
      const double gap = std::max({0.0, l - d, d - u});
      if (gap < best) {
        best = gap;
        low = l - gap;
        high = u + gap;
      }
    }
    auto* c = lp->MakeRowConstraint(std::min(low, d) - initial,
                                    std::max(high, d) - initial);
    c->SetCoefficient(x[local[a.dst]], 1);
    c->SetCoefficient(x[local[a.src]], -1);
  }
  if (solve(*lp, 1.0) != MPSolver::OPTIMAL) {
    return false;
  }
  double before_extent = 0, after_extent = 0;
  for (size_t i = 0; i < nodes.size(); ++i) {
    after[nodes[i]] = x[i]->solution_value();
    before_extent += std::abs(before[nodes[i]]);
    after_extent += std::abs(after[nodes[i]]);
  }
  // Numerical solver tolerances must not silently increase any residual.
  bool valid = after_extent <= before_extent + 1e-6;
  for (int id : nodes) {
    const auto [lo, hi] = domain(layer, id);
    valid &= after[id] >= lo - 1e-6 && after[id] <= hi + 1e-6;
  }
  for (int aid : arcs) {
    const auto& a = layer.arcs[aid];
    valid &= trimResidualDbu(layer, a, after)
             <= trimResidualDbu(layer, a, before) + 1e-6;
  }
  if (!valid) {
    for (int id : nodes) {
      after[id] = before[id];
    }
  }
  return valid;
}

// Use the same HiGHS integer backend as the validated offline experiment.
// Keep a SCIP fallback for OR-Tools distributions built without HiGHS.
bool legalize(const LayerNodes& layer,
              const std::vector<int>& nodes,
              const std::vector<int>& arcs,
              const std::vector<int>& local,
              std::vector<double>& after)
{
  std::unique_ptr<MPSolver> mip;
  if (MPSolver::SupportsProblemType(
          MPSolver::HIGHS_MIXED_INTEGER_PROGRAMMING)) {
    mip = solver("HIGHS_MIXED_INTEGER_PROGRAMMING");
  } else {
    mip = solver("SCIP");
  }
  mip->SetNumThreads(1).IgnoreError();
  mip->set_time_limit(5000);
  std::vector<MPVariable*> x;
  for (int id : nodes) {
    const auto [lo, hi] = domain(layer, id);
    x.push_back(mip->MakeIntVar(std::ceil(lo), std::floor(hi), ""));
  }
  std::vector<MPVariable*> violated, residual;
  for (int aid : arcs) {
    const auto& a = layer.arcs[aid];
    auto* s = x[local[a.src]];
    auto* d = x[local[a.dst]];
    const double initial2 = along2(layer.nodes[a.dst], layer.horizontal)
                            - along2(layer.nodes[a.src], layer.horizontal);
    const double low2 = initial2 + 2 * (d->lb() - s->ub());
    const double high2 = initial2 + 2 * (d->ub() - s->lb());
    double cap = 1;
    for (const auto& r : a.ranges) {
      if (r.has_low) {
        cap = std::max(cap, r.low2 - low2);
      }
      if (r.has_high) {
        cap = std::max(cap, high2 - r.high2);
      }
    }
    auto* v = mip->MakeBoolVar("");
    auto* t = mip->MakeIntVar(0, cap, "");
    violated.push_back(v);
    residual.push_back(t);
    auto* enable = mip->MakeRowConstraint(-inf, 0);
    enable->SetCoefficient(t, 1);
    enable->SetCoefficient(v, -cap);
    auto* choose = mip->MakeRowConstraint(1, 1);
    for (const auto& r : a.ranges) {
      auto* b = mip->MakeBoolVar("");
      choose->SetCoefficient(b, 1);
      if (r.has_low) {
        const double big = std::max(0.0, r.low2 - low2);
        auto* c = mip->MakeRowConstraint(r.low2 - big - initial2, inf);
        c->SetCoefficient(d, 2);
        c->SetCoefficient(s, -2);
        c->SetCoefficient(t, 1);
        c->SetCoefficient(b, -big);
      }
      if (r.has_high) {
        const double big = std::max(0.0, high2 - r.high2);
        auto* c = mip->MakeRowConstraint(-inf, r.high2 + big - initial2);
        c->SetCoefficient(d, 2);
        c->SetCoefficient(s, -2);
        c->SetCoefficient(t, -1);
        c->SetCoefficient(b, big);
      }
    }
  }
  auto* objective = mip->MutableObjective();
  auto setExtent = [&]() {
    objective->Clear();
    for (size_t i = 0; i < nodes.size(); ++i) {
      objective->SetCoefficient(x[i], layer.nodes[nodes[i]].high_end ? 1 : -1);
    }
  };
  auto capture = [&]() {
    for (size_t i = 0; i < nodes.size(); ++i) {
      after[nodes[i]] = std::round(x[i]->solution_value());
    }
  };
  auto solved = [](MPSolver::ResultStatus status) {
    return status == MPSolver::OPTIMAL || status == MPSolver::FEASIBLE;
  };
  for (auto* v : violated) {
    v->SetBounds(0, 0);
  }
  setExtent();
  auto status = solve(*mip, 5.0);
  if (solved(status)) {
    capture();
    return status == MPSolver::OPTIMAL;
  }
  if (status != MPSolver::INFEASIBLE) {
    return false;
  }
  for (auto* v : violated) {
    v->SetBounds(0, 1);
  }
  bool optimal = true;
  for (const auto* variables : {&violated, &residual}) {
    objective->Clear();
    for (auto* v : *variables) {
      objective->SetCoefficient(v, 1);
    }
    status = solve(*mip, 5.0);
    if (!solved(status)) {
      return false;
    }
    capture();
    optimal &= status == MPSolver::OPTIMAL;
    const double value = std::round(objective->Value());
    auto* fixed = mip->MakeRowConstraint(value, value);
    for (auto* v : *variables) {
      fixed->SetCoefficient(v, 1);
    }
  }
  setExtent();
  status = solve(*mip, 5.0);
  if (solved(status)) {
    capture();
  }
  return optimal && status == MPSolver::OPTIMAL;
}
}  // namespace

double trimResidualDbu(const LayerNodes& layer,
                       const Arc& arc,
                       const std::vector<double>& displacement)
{
  const double d2 = along2(layer.nodes[arc.dst], layer.horizontal)
                    - along2(layer.nodes[arc.src], layer.horizontal)
                    + 2 * (displacement[arc.dst] - displacement[arc.src]);
  double gap2 = inf;
  for (const auto& r : arc.ranges) {
    gap2 = std::min(gap2,
                    std::max({0.0,
                              r.has_low ? r.low2 - d2 : 0.0,
                              r.has_high ? d2 - r.high2 : 0.0}));
  }
  return gap2 * 0.5;
}

PostprocessResult postprocessLayer(const LayerNodes& layer,
                                   const std::vector<double>& before,
                                   PostprocessProgress* progress)
{
  if (before.size() != layer.nodes.size()) {
    throw std::invalid_argument("DEL postprocess displacement size mismatch");
  }
  PostprocessResult result;
  result.polished = before;
  result.polish_optimal = true;
  result.legalized = before;
  std::vector<int> parent(layer.nodes.size()), local(layer.nodes.size());
  std::iota(parent.begin(), parent.end(), 0);
  auto find = [&](int i) {
    while (i != parent[i]) {
      parent[i] = parent[parent[i]];
      i = parent[i];
    }
    return i;
  };
  for (const auto& a : layer.arcs) {
    parent[find(a.src)] = find(a.dst);
  }
  std::vector<std::vector<int>> nodes(layer.nodes.size()),
      arcs(layer.nodes.size());
  for (const auto& n : layer.nodes) {
    auto& group = nodes[find(n.id)];
    local[n.id] = group.size();
    group.push_back(n.id);
    const auto [lo, hi] = domain(layer, n.id);
    if (std::ceil(lo) > std::floor(hi)) {
      throw std::runtime_error("DEL postprocess has no integer domain point");
    }
    result.legalized[n.id] = std::clamp(
        std::round(result.polished[n.id]), std::ceil(lo), std::floor(hi));
  }
  for (size_t i = 0; i < layer.arcs.size(); ++i) {
    arcs[find(layer.arcs[i].src)].push_back(i);
  }
  if (progress) {
    progress->total
        = std::count_if(nodes.begin(), nodes.end(), [](const auto& group) {
            return !group.empty();
          });
  }
  struct WorkerResult
  {
    size_t components = 0;
    size_t nonoptimal = 0;
    bool polish_optimal = true;
    double polish_seconds = 0;
    double legalize_seconds = 0;
  };
  std::atomic<size_t> next{0};
  auto work = [&]() {
    WorkerResult stats;
    while (true) {
      const size_t i = next.fetch_add(1);
      if (i >= nodes.size()) {
        break;
      }
      if (nodes[i].empty()) {
        continue;
      }
      if (progress && progress->cancelled) {
        throw std::runtime_error("DEL postprocess cancelled");
      }
      ++stats.components;
      // Components have disjoint nodes and arcs. All vectors remain fixed in
      // size; each worker writes only its component's coordinates.
      if (arcs[i].empty()) {
        for (int id : nodes[i]) {
          const auto [lo, hi] = domain(layer, id);
          result.polished[id] = std::clamp(0.0, lo, hi);
          result.legalized[id] = std::clamp(0.0, std::ceil(lo), std::floor(hi));
        }
      } else {
        const auto start = std::chrono::steady_clock::now();
        stats.polish_optimal
            &= polish(layer, nodes[i], arcs[i], local, before, result.polished);
        const auto polished = std::chrono::steady_clock::now();
        stats.polish_seconds
            += std::chrono::duration<double>(polished - start).count();
        for (int id : nodes[i]) {
          const auto [lo, hi] = domain(layer, id);
          result.legalized[id] = std::clamp(
              std::round(result.polished[id]), std::ceil(lo), std::floor(hi));
        }
        if (!legalize(layer, nodes[i], arcs[i], local, result.legalized)) {
          ++stats.nonoptimal;
        }
        stats.legalize_seconds
            += std::chrono::duration<double>(std::chrono::steady_clock::now()
                                             - polished)
                   .count();
      }
      if (progress) {
        ++progress->completed;
      }
    }
    return stats;
  };
  std::vector<std::future<WorkerResult>> workers;
  for (size_t i = 0; i < std::min<size_t>(4, nodes.size()); ++i) {
    workers.push_back(std::async(std::launch::async, work));
  }
  for (auto& worker : workers) {
    const auto stats = worker.get();
    result.components += stats.components;
    result.nonoptimal_components += stats.nonoptimal;
    result.polish_optimal &= stats.polish_optimal;
    result.polish_worker_seconds += stats.polish_seconds;
    result.legalize_worker_seconds += stats.legalize_seconds;
  }
  // Independently check integer coordinates against every original node domain.
  for (const auto& n : layer.nodes) {
    const auto [lo, hi] = domain(layer, n.id);
    const double x = result.legalized[n.id];
    if (!std::isfinite(x) || x != std::round(x) || x < lo || x > hi) {
      throw std::runtime_error(
          "DEL postprocess produced an invalid node position");
    }
  }
  return result;
}

void writePostprocessReports(const std::string& report,
                             const std::vector<LayerNodes>& layers,
                             const std::vector<std::vector<double>>& before,
                             const std::vector<PostprocessResult>& results,
                             double dbu)
{
  auto path = [&](const char* suffix) {
    std::filesystem::path p(report);
    p.replace_filename(p.stem().string() + suffix);
    return p;
  };
  std::ofstream nodes(path("_postprocess_displacements.csv"));
  std::ofstream arcs(path("_conflicts.csv"));
  std::ofstream summary(path("_postprocess.csv"));
  if (!nodes || !arcs || !summary) {
    throw std::runtime_error("Cannot open DEL postprocess reports");
  }
  nodes << std::setprecision(17)
        << "layer,node_id,trim_index,before_displacement_um,polished_"
           "displacement_um,final_displacement_um,final_x_um,final_y_um\n";
  arcs << std::setprecision(17)
       << "layer,arc_id,type,src,dst,src_trim_index,src_x_um,src_y_um,dst_trim_"
          "index,dst_x_um,dst_y_um,repair_distance_nm,below_5nm,status\n";
  summary << std::setprecision(17)
          << "layer,before_extent_um,polished_extent_um,final_extent_um,exact_"
             "conflicts,A,B,C,below_5nm,components,nonoptimal_components,"
             "polish_optimal\n";
  for (size_t l = 0; l < layers.size(); ++l) {
    const auto& layer = layers[l];
    const auto& r = results[l];
    auto coords = [&](int id) {
      const auto& n = layer.nodes[id];
      return std::pair{
          (n.center_x2 * 0.5 + (layer.horizontal ? r.legalized[id] : 0)) / dbu,
          (n.center_y2 * 0.5 + (layer.horizontal ? 0 : r.legalized[id])) / dbu};
    };
    for (const auto& n : layer.nodes) {
      const auto [x, y] = coords(n.id);
      nodes << layer.layer << ',' << n.id << ',' << n.trim_index << ','
            << before[l][n.id] / dbu << ',' << r.polished[n.id] / dbu << ','
            << r.legalized[n.id] / dbu << ',' << x << ',' << y << '\n';
    }
    std::array<size_t, 3> count{};
    size_t below = 0;
    for (const auto& a : layer.arcs) {
      const double gap = trimResidualDbu(layer, a, r.legalized);
      if (gap <= 0) {
        continue;
      }
      ++count[static_cast<size_t>(a.type)];
      const double nm = gap * 1000 / dbu;
      below += nm < 5;
      const auto [sx, sy] = coords(a.src);
      const auto [dx, dy] = coords(a.dst);
      arcs << layer.layer << ',' << a.id << ',' << toString(a.type) << ','
           << a.src << ',' << a.dst << ',' << layer.nodes[a.src].trim_index
           << ',' << sx << ',' << sy << ',' << layer.nodes[a.dst].trim_index
           << ',' << dx << ',' << dy << ',' << nm << ',' << (nm < 5)
           << ",unrepaired\n";
    }
    summary << layer.layer << ',' << extent(before[l]) / dbu << ','
            << extent(r.polished) / dbu << ',' << extent(r.legalized) / dbu
            << ',' << count[0] + count[1] + count[2] << ',' << count[0] << ','
            << count[1] << ',' << count[2] << ',' << below << ','
            << r.components << ',' << r.nonoptimal_components << ','
            << r.polish_optimal << '\n';
  }
  nodes.close();
  arcs.close();
  summary.close();
  if (!nodes || !arcs || !summary) {
    throw std::runtime_error("Failed to write DEL postprocess reports");
  }
}
}  // namespace del
