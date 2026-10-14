// SPDX-License-Identifier: BSD-3-Clause
#include "RunOptimization.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <stdexcept>

#include "../GraphGraphics.h"
#include "delgpu/GpuOptimizer.h"
#include "delgpu/Optimizer.h"
#include "odb/db.h"
#include "odb/dbBlockCallBackObj.h"
#include "utl/Logger.h"
#include "web/core.h"
namespace delgpu {
namespace {
unsigned long long count(const TrimStats& s)
{
  return s.violations[0] + s.violations[1] + s.violations[2];
}
unsigned long long remaining(const TrimStats& s)
{
  return s.remaining[0] + s.remaining[1] + s.remaining[2];
}
TrimStats convert(const OptimizationStats& s)
{
  TrimStats t;
  t.energy = s.energy;
  t.rule_energy = s.rule_energy;
  t.extent_dbu = s.extent_dbu;
  t.max_violation_dbu = s.max_violation_dbu;
  t.max_step_dbu = s.max_step_dbu;
  t.moved = s.moved;
  t.repaired = s.stall_repaired;
  t.accepted = s.accepted;
  for (int i = 0; i < 3; ++i) {
    t.violations[i] = s.violations[i];
    t.remaining[i] = s.remaining_violations[i];
  }
  return t;
}
}  // namespace
void runOptimization(odb::dbBlock* block,
                     const std::vector<LayerNodes>& layers,
                     bool gpu,
                     bool gui,
                     int iterations,
                     double max_step,
                     double jump_offset_nm,
                     int frame_ms,
                     const std::string& report,
                     int max_backtracks,
                     utl::Logger* logger)
{
  using Clock = std::chrono::steady_clock;
  const auto begin = Clock::now();
  odb::dbBlockCallBackObj owner;
  owner.addOwner(block);
  const double tolerance = 5.0 * block->getDbUnitsPerMicron() / 1000.0;
  std::ofstream csv;
  if (!report.empty()) {
    csv.open(report);
    if (!csv) {
      throw std::runtime_error("Cannot open iteration report: " + report);
    }
    csv << "layer,stage,iteration,lambda,energy,conflicts,remaining_5nm,max_"
           "gap_dbu,extent_dbu,candidates,repairs,elapsed_ms\n";
    csv << std::setprecision(12);
  }
  auto write = [&](const std::string& layer,
                   const char* stage,
                   int i,
                   double lambda,
                   const TrimStats& s) {
    if (!csv.is_open()) {
      return;
    }
    const double elapsed
        = std::chrono::duration<double, std::milli>(Clock::now() - begin)
              .count();
    csv << layer << ',' << stage << ',' << i << ',' << lambda << ',' << s.energy
        << ',' << count(s) << ',' << remaining(s) << ',' << s.max_violation_dbu
        << ',' << s.extent_dbu << ',' << s.candidates << ',' << s.repaired
        << ',' << elapsed << '\n';
    if (!csv) {
      throw std::runtime_error("Failed to write iteration report");
    }
  };
  unsigned long long initial_total = 0, final_total = 0, remaining_total = 0,
                     total_candidates = 0, total_repairs = 0;
  size_t snapshot_bytes = 0;
  double total_extent_dbu = 0, total_wire_extension_dbu = 0;
  double iteration_ms = 0;
  for (size_t layer_id = 0; layer_id < layers.size(); ++layer_id) {
    const auto& layer = layers[layer_id];
    if (layer.nodes.empty()) {
      continue;
    }
    const auto start = Clock::now();
    std::unique_ptr<TrimOptimizer> device;
    std::unique_ptr<LayerOptimizer> cpu;
    if (gpu) {
      device = makeGpuOptimizer(layer,
                                tolerance,
                                max_step,
                                block->getDbUnitsPerMicron(),
                                jump_offset_nm,
                                max_backtracks);
    } else {
      cpu = std::make_unique<LayerOptimizer>(layer,
                                             tolerance,
                                             block->getDbUnitsPerMicron(),
                                             jump_offset_nm,
                                             max_step);
    }
    const auto ready = Clock::now();
    auto stats = gpu ? device->evaluate(10) : convert(cpu->evaluate(10));
    const auto initial_count = count(stats);
    initial_total += initial_count;
    write(layer.layer, "initial", 0, 10, stats);
    auto last_frame = Clock::now();
    int completed = 0;
    double lambda = 10;
    for (int iteration = 1; iteration <= iterations && count(stats) > 0;
         ++iteration) {
      lambda = 10.0 * (1 << std::min(3, (iteration - 1) / 25));
      stats = gpu ? device->step(lambda) : convert(cpu->step(lambda));
      completed = iteration;
      total_candidates += stats.candidates;
      total_repairs += stats.repaired;
      write(layer.layer, "iteration", iteration, lambda, stats);
      if (iteration % 25 == 0 || count(stats) == 0 || iteration == iterations) {
        logger->report(
            "DELGPU iteration: layer={} iter={} lambda={} conflicts={} "
            "remaining_5nm={} max_gap_dbu={:.6f} candidates={} repairs={}",
            layer.layer,
            iteration,
            lambda,
            count(stats),
            remaining(stats),
            stats.max_violation_dbu,
            stats.candidates,
            stats.repaired);
      }
      if (gui && frame_ms > 0
          && std::chrono::duration<double, std::milli>(Clock::now()
                                                       - last_frame)
                     .count()
                 >= frame_ms) {
        auto snapshot
            = gpu ? device->displacementDbu() : cpu->displacementDbu();
        if (gpu) {
          snapshot_bytes += snapshot.size() * sizeof(double);
        }
        updateGraphGraphics(layer_id, std::move(snapshot));
        web::Gui::get()->pause(1);
        if (!owner.hasOwner()) {
          throw std::runtime_error(
              "DELGPU stopped: design was destroyed during GUI update");
        }
        last_frame = Clock::now();
      }
    }
    const auto iterated = Clock::now();
    iteration_ms
        += std::chrono::duration<double, std::milli>(iterated - ready).count();
    // Keep the last iteration, matching native DEL's continuous-stage result.
    auto final_positions
        = gpu ? device->displacementDbu() : cpu->displacementDbu();
    if (gpu) {
      snapshot_bytes += final_positions.size() * sizeof(double);
    }
    const auto assessed = assessDisplacement(layer, final_positions, tolerance);
    // The independent host check is authoritative for final conflict counts.
    if (gpu) {
      for (int i = 0; i < 3; ++i) {
        if (stats.violations[i] != assessed.violations[i]
            || stats.remaining[i] != assessed.remaining[i]) {
          throw std::runtime_error(
              "GPU final quality statistics differ: type=" + std::to_string(i)
              + " gpu=" + std::to_string(stats.violations[i])
              + " host=" + std::to_string(assessed.violations[i])
              + " gpu_5nm=" + std::to_string(stats.remaining[i])
              + " host_5nm=" + std::to_string(assessed.remaining[i]));
        }
      }
    }
    for (int i = 0; i < 3; ++i) {
      stats.violations[i] = assessed.violations[i];
      stats.remaining[i] = assessed.remaining[i];
    }
    stats.max_violation_dbu = assessed.max_violation_dbu;
    stats.extent_dbu = assessed.extent_dbu;
    double wire_extension_dbu = 0;
    for (size_t i = 0; i < final_positions.size(); ++i) {
      wire_extension_dbu
          += layer.nodes[i].high_end ? final_positions[i] : -final_positions[i];
    }
    total_extent_dbu += stats.extent_dbu;
    total_wire_extension_dbu += wire_extension_dbu;
    final_total += count(stats);
    remaining_total += remaining(stats);
    write(layer.layer, "final", completed, lambda, stats);
    if (gui) {
      updateGraphGraphics(layer_id, std::move(final_positions));
    }
    const auto end = Clock::now();
    logger->report(
        "DELGPU optimization: layer={} iterations={} initial_conflicts={} "
        "final_conflicts={} remaining_5nm={} max_gap_dbu={:.6f} "
        "extent_um={:.6f} wire_extension_um={:.6f} "
        "setup_ms={:.3f} iterate_ms={:.3f} check_ms={:.3f} device_mib={:.2f}",
        layer.layer,
        completed,
        initial_count,
        count(stats),
        remaining(stats),
        stats.max_violation_dbu,
        stats.extent_dbu / block->getDbUnitsPerMicron(),
        wire_extension_dbu / block->getDbUnitsPerMicron(),
        std::chrono::duration<double, std::milli>(ready - start).count(),
        std::chrono::duration<double, std::milli>(iterated - ready).count(),
        std::chrono::duration<double, std::milli>(end - iterated).count(),
        gpu ? device->deviceBytes() / (1024.0 * 1024.0) : 0.0);
  }
  logger->report(
      "DELGPU optimization complete: backend={} gui={} initial_conflicts={} "
      "final_conflicts={} remaining_5nm={} candidates={} repairs={} "
      "extent_um={:.6f} wire_extension_um={:.6f} "
      "coordinate_download_bytes={} iteration_ms={:.3f} total_ms={:.3f}",
      gpu ? "gpu" : "cpu",
      gui,
      initial_total,
      final_total,
      remaining_total,
      total_candidates,
      total_repairs,
      total_extent_dbu / block->getDbUnitsPerMicron(),
      total_wire_extension_dbu / block->getDbUnitsPerMicron(),
      snapshot_bytes,
      iteration_ms,
      std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
}
}  // namespace delgpu
