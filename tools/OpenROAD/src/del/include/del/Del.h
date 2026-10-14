// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, The OpenROAD Authors

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "del/Graph.h"
#include "del/Trim.h"

namespace odb {
class dbBlock;
class dbTechLayer;
}  // namespace odb

namespace utl {
class Logger;
}

namespace del {

// Persistent defaults for subsequent runs. Each run snapshots these values.
struct DelParameters
{
  int max_iterations = 200;
  double max_step_w = 0.2;
  double jump_offset_nm = 5.0;
};
extern DelParameters global_parameters;

struct OptimizationSession;

class Del
{
 public:
  explicit Del(utl::Logger* logger);

  using FrameCallback
      = std::function<bool(int, const std::vector<std::vector<double>>&)>;
  void configure(int max_iterations, double max_step_w, double jump_offset_nm);
  void run(int max_iterations = -1,
           const std::string& report = "",
           const FrameCallback& frame = {},
           bool step_mode = false);
  void next(const FrameCallback& frame = {});
  void continueRun(const FrameCallback& frame = {});
  const std::vector<std::vector<double>>& getDisplacements() const
  {
    return displacements_;
  }
  const std::vector<Trim>& getTrims() const { return trims_; }
  bool isPostprocessed() const { return postprocessed_; }
  const std::vector<LayerNodes>& getLayerNodes() const { return layer_nodes_; }

 private:
  odb::dbBlock* init();
  void buildGraph(odb::dbBlock* block, odb::dbTechLayer* layer);

  void optimize(int max_iterations,
                double dbu_per_micron,
                const std::string& report,
                const FrameCallback& frame,
                bool step_mode,
                odb::dbBlock* block,
                const DelParameters& parameters);
  bool emitOptimizationFrame(const FrameCallback& frame);
  void finishOptimization(const FrameCallback& frame);
  std::shared_ptr<OptimizationSession> session_;
  std::vector<std::vector<double>> displacements_;
  bool running_ = false;
  bool postprocessed_ = false;
  utl::Logger* logger_;
  std::vector<Trim> trims_;
  std::vector<LayerNodes> layer_nodes_;
};

}  // namespace del
