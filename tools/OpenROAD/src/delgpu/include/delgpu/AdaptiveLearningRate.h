// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace delgpu {
// One controller per layer. Uses existing summaries, never extra GPU readbacks.
class AdaptiveLearningRate
{
 public:
  AdaptiveLearningRate(double start,
                       double floor,
                       int iterations,
                       double initial_rule,
                       unsigned long long initial_remaining)
      : start_(start),
        floor_(floor),
        rate_(start),
        iterations_(iterations),
        window_rule_(initial_rule),
        window_remaining_(initial_remaining)
  {
    if (!std::isfinite(start) || !std::isfinite(floor) || floor <= 0
        || start < floor || iterations < 1) {
      throw std::invalid_argument("Invalid adaptive learning-rate bounds");
    }
  }

  double rate(int iteration)
  {
    if (iteration < 1 || iteration > iterations_) {
      throw std::invalid_argument("Invalid adaptive learning-rate iteration");
    }
    const double t
        = iterations_ == 1 ? 0 : double(iteration - 1) / (iterations_ - 1);
    constexpr double pi = 3.14159265358979323846;
    const double ceiling
        = floor_ + (start_ - floor_) * (0.5 * (1 + std::cos(pi * t)));
    rate_ = std::min(rate_, ceiling);
    return rate_;
  }

  void observe(double rule,
               unsigned long long remaining,
               bool gradient_accepted)
  {
    rejected_ = gradient_accepted ? 0 : rejected_ + 1;
    ++window_;
    // A successful barrier jump must not masquerade as gradient acceptance.
    if (rejected_ >= 2) {
      reduce(0.8);
      rejected_ = 0;
      resetWindow(rule, remaining);
    } else if (window_ >= 8) {
      // Rule energy is independent of the changing penalty weight lambda.
      if (rule >= window_rule_ * 0.99 && remaining >= window_remaining_) {
        reduce(0.85);
      }
      resetWindow(rule, remaining);
    }
  }

  int reductions() const { return reductions_; }

 private:
  void reduce(double factor)
  {
    const double next = std::max(floor_, rate_ * factor);
    if (next < rate_) {
      ++reductions_;
    }
    rate_ = next;
  }
  void resetWindow(double rule, unsigned long long remaining)
  {
    window_ = 0;
    window_rule_ = rule;
    window_remaining_ = remaining;
  }

  double start_, floor_, rate_;
  int iterations_, rejected_ = 0, window_ = 0, reductions_ = 0;
  double window_rule_;
  unsigned long long window_remaining_;
};
}  // namespace delgpu
