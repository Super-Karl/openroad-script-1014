// SPDX-License-Identifier: BSD-3-Clause
#include <stdexcept>

#include "delgpu/TrimOptimizer.h"
namespace delgpu {
class TrimOptimizer::Impl
{
};
TrimOptimizer::TrimOptimizer(const std::vector<OptNode>&,
                             const std::vector<OptEdge>&,
                             const std::vector<OptRange>&,
                             const std::vector<int>&,
                             const std::vector<int>&,
                             const std::vector<int>&,
                             const std::vector<int>&,
                             double,
                             double,
                             double,
                             double,
                             int,
                             double,
                             double)
{
  throw std::runtime_error("GPU iteration backend was not compiled");
}
TrimOptimizer::~TrimOptimizer() = default;
TrimStats TrimOptimizer::evaluate(double)
{
  throw std::runtime_error("CUDA unavailable");
}
TrimStats TrimOptimizer::step(double)
{
  throw std::runtime_error("CUDA unavailable");
}
TrimStats TrimOptimizer::step(double, double)
{
  throw std::runtime_error("CUDA unavailable");
}
PostprocessResult TrimOptimizer::postprocess(const PostprocessGraph&,
                                             const PostprocessOptions&)
{
  throw std::runtime_error("CUDA unavailable");
}
void TrimOptimizer::setDisplacementDbu(const std::vector<double>&)
{
  throw std::runtime_error("CUDA unavailable");
}
std::vector<double> TrimOptimizer::displacementDbu()
{
  throw std::runtime_error("CUDA unavailable");
}
size_t TrimOptimizer::deviceBytes() const
{
  return 0;
}
}  // namespace delgpu
