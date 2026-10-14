// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <iomanip>
#include <stdexcept>

#include "ResidualReport.h"
#include "del/Del.h"
#include "del/Optimizer.h"
#include "del/Postprocess.h"
#include "odb/dbBlockCallBackObj.h"
#include "utl/Logger.h"

namespace del {
namespace {
size_t violationCount(const OptimizationStats& s)
{
  return s.violations[0] + s.violations[1] + s.violations[2];
}
size_t remainingCount(const OptimizationStats& s)
{
  return s.remaining_violations[0] + s.remaining_violations[1]
         + s.remaining_violations[2];
}
void accumulate(OptimizationStats& sum, const OptimizationStats& s)
{
  sum.energy += s.energy;
  sum.rule_energy += s.rule_energy;
  sum.extent_dbu += s.extent_dbu;
  sum.max_violation_dbu = std::max(sum.max_violation_dbu, s.max_violation_dbu);
  sum.max_step_dbu = std::max(sum.max_step_dbu, s.max_step_dbu);
  sum.moved += s.moved;
  sum.stall_triggered += s.stall_triggered;
  sum.stall_repaired += s.stall_repaired;
  sum.stall_unresolved += s.stall_unresolved;
  sum.max_repair_dbu = std::max(sum.max_repair_dbu, s.max_repair_dbu);
  for (size_t i = 0; i < 3; ++i) {
    sum.violations[i] += s.violations[i];
    sum.remaining_violations[i] += s.remaining_violations[i];
  }
}
void writeRow(std::ofstream& csv,
              int iteration,
              const std::string& layer,
              double lambda,
              const OptimizationStats& s,
              double dbu,
              double seconds)
{
  if (!csv.is_open()) {
    return;
  }
  csv << iteration << ',' << layer << ',' << lambda << ',' << s.energy << ','
      << s.rule_energy << ',' << s.extent_dbu / dbu << ',' << s.violations[0]
      << ',' << s.violations[1] << ',' << s.violations[2] << ','
      << s.max_violation_dbu / dbu << ',' << s.max_step_dbu / dbu << ','
      << s.moved << ',' << seconds << ',' << s.stall_triggered << ','
      << s.stall_repaired << ',' << s.stall_unresolved << ','
      << s.max_repair_dbu / dbu << ',' << remainingCount(s) << ','
      << violationCount(s) - remainingCount(s) << ','
      << s.remaining_violations[0] << ',' << s.remaining_violations[1] << ','
      << s.remaining_violations[2] << ',' << s.stall_repaired << '\n';
  if (!csv) {
    throw std::runtime_error("Failed to write DEL iteration report");
  }
}
void reportAssessment(utl::Logger* logger,
                      int max_iterations,
                      const std::vector<OptimizationStats>& history)
{
  if (max_iterations > 0) {
    const auto& first = history.front();
    const auto& last = history.back();
    const auto& recent = history[history.size() > 26 ? history.size() - 26 : 0];
    const double progress = (recent.rule_energy - last.rule_energy)
                            / std::max(1e-12, recent.rule_energy);
    logger->report(
        "DEL assessment: rule_energy {:.6f} -> {:.6f}; exact violations {} -> "
        "{}; last {} iterations rule reduction={:.2f}%; {}",
        first.rule_energy,
        last.rule_energy,
        violationCount(first),
        violationCount(last),
        std::min(25, max_iterations),
        100 * progress,
        progress > 0.01 ? "still improving; evaluate a larger budget next"
                        : "little recent improvement; inspect constraints and "
                          "tuning before adding iterations");
    logger->report(
        "DEL tolerance assessment (<{}nm treated as repaired): remaining {} "
        "-> {}; final positive residuals below threshold={}",
        kRepairToleranceNm,
        remainingCount(first),
        remainingCount(last),
        violationCount(last) - remainingCount(last));
    logger->report(
        "DEL continuous stage stopped at iteration budget {}; starting trim "
        "postprocess (extent polish and exact conflict legalization)",
        max_iterations);
  }
}
void reportIteration(utl::Logger* logger,
                     int iteration,
                     int max_iterations,
                     double lambda,
                     const OptimizationStats& total,
                     double dbu,
                     double seconds)
{
  logger->report(
      "DEL iteration {}/{}: lambda={} energy={:.6f} rule={:.6f} "
      "violations={} (A={} B={} C={}) extent={:.6f}um max_violation={:.6f}um "
      "max_step={:.6f}um moved={} elapsed={:.3f}s "
      "stall_triggered={} stall_jumped={} stall_unresolved={} "
      "max_repair={:.6f}um remaining_5nm={} treated_repaired_below_5nm={}",
      iteration,
      max_iterations,
      lambda,
      total.energy,
      total.rule_energy,
      violationCount(total),
      total.violations[0],
      total.violations[1],
      total.violations[2],
      total.extent_dbu / dbu,
      total.max_violation_dbu / dbu,
      total.max_step_dbu / dbu,
      total.moved,
      seconds,
      total.stall_triggered,
      total.stall_repaired,
      total.stall_unresolved,
      total.max_repair_dbu / dbu,
      remainingCount(total),
      violationCount(total) - remainingCount(total));
}
}  // namespace

// Retain solver state across Tcl commands; OpenDB detaches the callback when
// the source block is destroyed, so `n` cannot reuse a reloaded database.
struct OptimizationSession : odb::dbBlockCallBackObj
{
  std::ofstream csv;
  std::vector<LayerOptimizer> optimizers;
  std::vector<OptimizationStats> history;
  std::string report;
  double dbu = 1;
  double elapsed = 0;
  int iteration = 0;
  int limit = 0;
  bool finished = false;
};

void Del::optimize(int max_iterations,
                   double dbu,
                   const std::string& report,
                   const FrameCallback& frame,
                   bool step_mode,
                   odb::dbBlock* block,
                   const DelParameters& parameters)
{
  session_ = std::make_shared<OptimizationSession>();
  auto& state = *session_;
  if (block != nullptr) {
    state.addOwner(block);
  }
  state.dbu = dbu;
  state.report = report;
  state.limit = max_iterations;
  if (!report.empty()) {
    state.csv.open(report);
    if (!state.csv) {
      throw std::runtime_error("Cannot open DEL report: " + report);
    }
    state.csv
        << std::setprecision(12)
        << "iteration,layer,lambda,energy,rule_energy,extent_um,A_violations,B_"
           "violations,C_violations,max_violation_um,max_step_um,moved_nodes,"
           "elapsed_s,stall_triggered,stall_repaired,stall_unresolved,max_"
           "repair_um,remaining_5nm,treated_repaired_below_5nm,"
           "A_remaining_5nm,B_remaining_5nm,C_remaining_5nm,stall_jumped\n";
  }
  for (const auto& layer : layer_nodes_) {
    state.optimizers.emplace_back(layer,
                                  kRepairToleranceNm * dbu / 1000.0,
                                  dbu,
                                  parameters.jump_offset_nm,
                                  parameters.max_step_w);
  }
  if (state.optimizers.empty()) {
    if (frame) {
      frame(0, {});
    }
    session_.reset();
    return;
  }
  logger_->report(
      "DEL optimize: max_iterations={} alpha=0.01 epsilon=0.01W "
      "gradient_max_step={}W "
      "lambda=10/20/40/80 (25 iterations each, then held at 80); violated arcs "
      "with both "
      "endpoints stationary for 3 rounds jump {}nm past the barrier midpoint; "
      "continuous overlay only",
      max_iterations,
      parameters.max_step_w,
      parameters.jump_offset_nm);
  if (!emitOptimizationFrame(frame)) {
    return;
  }
  if (step_mode && max_iterations > 0) {
    logger_->report(
        "DEL step mode: iteration 0/{}; enter n in the Tcl console for the "
        "next iteration, or c to continue to the iteration limit",
        max_iterations);
    return;
  }
  while (state.iteration < state.limit) {
    ++state.iteration;
    if (!emitOptimizationFrame(frame)) {
      return;
    }
  }
  finishOptimization(frame);
}

bool Del::emitOptimizationFrame(const FrameCallback& frame)
{
  auto& state = *session_;
  const auto start = std::chrono::steady_clock::now();
  const auto elapsed = [&]() {
    return state.elapsed
           + std::chrono::duration<double>(std::chrono::steady_clock::now()
                                           - start)
                 .count();
  };
  const int iteration = state.iteration;
  const double lambda
      = 10
        * std::pow(2.0, iteration == 0 ? 0 : std::min(3, (iteration - 1) / 25));
  OptimizationStats total;
  std::vector<std::vector<double>> positions;
  for (size_t l = 0; l < state.optimizers.size(); ++l) {
    const auto stats = iteration == 0 ? state.optimizers[l].evaluate(lambda)
                                      : state.optimizers[l].step(lambda);
    accumulate(total, stats);
    positions.push_back(state.optimizers[l].displacementDbu());
    writeRow(state.csv,
             iteration,
             layer_nodes_[l].layer,
             lambda,
             stats,
             state.dbu,
             elapsed());
  }
  const double seconds = elapsed();
  writeRow(state.csv, iteration, "TOTAL", lambda, total, state.dbu, seconds);
  if (state.csv.is_open()) {
    state.csv.flush();
    if (!state.csv) {
      throw std::runtime_error("Failed to flush DEL iteration report");
    }
  }
  state.history.push_back(total);
  reportIteration(
      logger_, iteration, state.limit, lambda, total, state.dbu, seconds);
  displacements_ = std::move(positions);
  const bool displayed = !frame || frame(iteration, displacements_);
  state.elapsed = elapsed();
  if (!displayed || !state.hasOwner()) {
    logger_->report(
        "DEL optimization stopped: displayed database was released");
    session_.reset();
    return false;
  }
  return true;
}

void Del::finishOptimization(const FrameCallback& frame)
{
  const auto retained = session_;
  auto& state = *retained;
  reportAssessment(logger_, state.limit, state.history);
  if (!state.report.empty()) {
    logger_->report(
        "DEL continuous-stage residuals (nm): {}",
        writeResidualReport(
            state.report, layer_nodes_, state.optimizers, state.dbu));
    logger_->report(
        "DEL continuous-stage unrepaired conflicts (residual >= {}nm): {}",
        kRepairToleranceNm,
        writeResidualReport(
            state.report, layer_nodes_, state.optimizers, state.dbu, true));
    logger_->report(
        "DEL continuous-stage endpoint displacements (um; overlay only): {}",
        writeDisplacementReport(
            state.report, layer_nodes_, state.optimizers, state.dbu));
  }
  if (state.limit > 0) {
    const auto before = displacements_;
    std::vector<PostprocessResult> results;
    std::array<size_t, 3> total{};
    size_t below = 0, nonoptimal = 0;
    double before_extent = 0, polished_extent = 0, final_extent = 0;
    for (size_t l = 0; l < layer_nodes_.size(); ++l) {
      const auto& layer = layer_nodes_[l];
      logger_->report(
          "DEL postprocess {}: extent polish, then integer trim legalization",
          layer.layer);
      // Solver work owns plain graph/coordinate copies. Only this main thread
      // touches the logger, GUI and OpenDB session callback.
      auto progress = std::make_shared<PostprocessProgress>();
      auto pending = std::async(
          std::launch::async, [layer, initial = before[l], progress]() {
            return postprocessLayer(layer, initial, progress.get());
          });
      const auto started = std::chrono::steady_clock::now();
      auto last_log = started;
      try {
        while (pending.wait_for(std::chrono::milliseconds(100))
               != std::future_status::ready) {
          const auto now = std::chrono::steady_clock::now();
          if (now - last_log >= std::chrono::seconds(5)) {
            logger_->report(
                "DEL postprocess {}: {}/{} components, elapsed={:.1f}s",
                layer.layer,
                progress->completed.load(),
                progress->total.load(),
                std::chrono::duration<double>(now - started).count());
            last_log = now;
          }
          // Same iteration: the GUI services events without rebuilding its
          // graph/boxes. The final exact frame is published only after success.
          if (!state.hasOwner()
              || (frame && !frame(state.iteration, displacements_))) {
            progress->cancelled = true;
            pending.wait();
            session_.reset();
            logger_->report(
                "DEL postprocess stopped: displayed database was released");
            return;
          }
        }
        results.push_back(pending.get());
      } catch (...) {
        progress->cancelled = true;
        if (pending.valid()) {
          pending.wait();
        }
        throw;
      }
      const auto& r = results.back();
      nonoptimal += r.nonoptimal_components;
      for (size_t i = 0; i < before[l].size(); ++i) {
        before_extent += std::abs(before[l][i]);
        polished_extent += std::abs(r.polished[i]);
        final_extent += std::abs(r.legalized[i]);
      }
      std::array<size_t, 3> counts{};
      for (const auto& a : layer.arcs) {
        const double gap = trimResidualDbu(layer, a, r.legalized);
        if (gap > 0) {
          ++counts[static_cast<size_t>(a.type)];
          below += gap < kRepairToleranceNm * state.dbu / 1000;
        }
      }
      for (size_t i = 0; i < 3; ++i) {
        total[i] += counts[i];
      }
      logger_->report(
          "DEL postprocess {}: conflicts={} (A={} B={} C={}) components={} "
          "nonoptimal_components={} polish_optimal={} elapsed={:.3f}s "
          "polish_worker_seconds={:.3f} legalize_worker_seconds={:.3f}",
          layer.layer,
          counts[0] + counts[1] + counts[2],
          counts[0],
          counts[1],
          counts[2],
          r.components,
          r.nonoptimal_components,
          r.polish_optimal,
          std::chrono::duration<double>(std::chrono::steady_clock::now()
                                        - started)
              .count(),
          r.polish_worker_seconds,
          r.legalize_worker_seconds);
    }
    if (!state.report.empty()) {
      writePostprocessReports(
          state.report, layer_nodes_, before, results, state.dbu);
      logger_->report(
          "DEL postprocess CSVs: *_postprocess.csv, "
          "*_postprocess_displacements.csv, *_conflicts.csv (report prefix: "
          "{})",
          state.report);
    }
    for (size_t l = 0; l < results.size(); ++l) {
      displacements_[l] = std::move(results[l].legalized);
    }
    postprocessed_ = true;
    logger_->report(
        "DEL final trim conflicts={} (A={} B={} C={}); below_5nm={} (still "
        "conflicts); extent={:.6f} -> {:.6f} -> {:.6f}um (continuous -> polish "
        "-> legalized); nonoptimal_components={}; ODB unchanged",
        total[0] + total[1] + total[2],
        total[0],
        total[1],
        total[2],
        below,
        before_extent / state.dbu,
        polished_extent / state.dbu,
        final_extent / state.dbu,
        nonoptimal);
    if ((frame && !frame(state.iteration, displacements_))
        || !state.hasOwner()) {
      session_.reset();
      return;
    }
  }
  state.csv.close();
  state.finished = true;
}

void Del::next(const FrameCallback& frame)
{
  if (running_) {
    throw std::runtime_error("DEL is already running");
  }
  if (!session_ || !session_->hasOwner()) {
    session_.reset();
    throw std::runtime_error("No active DEL session; run del -step first");
  }
  if (session_->finished) {
    logger_->report(
        "DEL has reached its {}-iteration limit; run del -step to restart",
        session_->limit);
    return;
  }
  running_ = true;
  struct Guard
  {
    bool& running;
    ~Guard() { running = false; }
  } guard{running_};
  try {
    ++session_->iteration;
    if (!emitOptimizationFrame(frame)) {
      return;
    }
    if (session_->iteration == session_->limit) {
      finishOptimization(frame);
    }
  } catch (...) {
    session_.reset();
    throw;
  }
}
void Del::continueRun(const FrameCallback& frame)
{
  if (running_) {
    throw std::runtime_error("DEL is already running");
  }
  if (!session_ || !session_->hasOwner()) {
    session_.reset();
    throw std::runtime_error("No active DEL session; run del -step first");
  }
  if (session_->finished) {
    next(frame);
    return;
  }
  running_ = true;
  struct Guard
  {
    bool& running;
    ~Guard() { running = false; }
  } guard{running_};
  try {
    while (session_->iteration < session_->limit) {
      ++session_->iteration;
      if (!emitOptimizationFrame(frame)) {
        return;
      }
    }
    finishOptimization(frame);
  } catch (...) {
    session_.reset();
    throw;
  }
}
}  // namespace del
