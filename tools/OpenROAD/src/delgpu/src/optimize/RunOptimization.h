// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <string>

#include "delgpu/Graph.h"
#include "delgpu/Postprocess.h"
namespace odb {
class dbBlock;
}
namespace utl {
class Logger;
}
namespace delgpu {
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
                     double learning_rate,
                     double learning_rate_end,
                     const std::string& learning_rate_mode,
                     bool postprocess,
                     const PostprocessOptions& post_options,
                     utl::Logger* logger);
}
