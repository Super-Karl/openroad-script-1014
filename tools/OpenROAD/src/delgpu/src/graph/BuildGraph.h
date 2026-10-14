// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "delgpu/Graph.h"
namespace odb {
class dbBlock;
class dbTechLayer;
}  // namespace odb
namespace utl {
class Logger;
}
namespace delgpu {
LayerNodes buildLayerGraph(odb::dbBlock* block,
                           odb::dbTechLayer* layer,
                           size_t trim_offset,
                           bool gpu,
                           bool verify,
                           utl::Logger* logger);
}
