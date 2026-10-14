// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <string>

#include "delgpu/Graph.h"
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
                     utl::Logger* logger);
}
