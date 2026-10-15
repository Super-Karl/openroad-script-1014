// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace odb {
class dbBlock;
}
namespace utl {
class Logger;
}
namespace leo {
class Leo;
struct LayerNodes;
struct GraphQuality
{
  size_t nodes = 0, arcs = 0, initial_violations = 0, initial_remaining = 0;
  size_t violations = 0, remaining = 0, unselected = 0, selected_violations = 0;
  std::array<size_t, 3> by_type{}, remaining_by_type{};
  double extent_dbu = 0, extension_dbu = 0;
  int64_t max_gap2 = 0;
};
GraphQuality assessLayer(const LayerNodes& layer,
                         double dbu,
                         bool final_positions = true);
// Inspect initial and final graph positions without changing the baseline.
void reportQuality(const Leo& tool,
                   odb::dbBlock* block,
                   const std::string& report,
                   utl::Logger* logger);
// Replay the original solvers on copies to isolate LPA and EMA contributions.
void auditStages(const Leo& tool,
                 odb::dbBlock* block,
                 const std::string& report,
                 utl::Logger* logger);
}  // namespace leo
