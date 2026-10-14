// SPDX-License-Identifier: BSD-3-Clause
#include <random>

#include "delgpu/Gpu.h"
#include "delgpu/GraphGpu.h"
#include "gtest/gtest.h"

namespace delgpu {
namespace {
void compare(LayerNodes layer, const std::vector<odb::Rect>& obstacles)
{
  auto cpu = layer;
  computeDomains(cpu, obstacles);
  buildArcs(cpu);
  buildGraphGpu(layer, obstacles);
  EXPECT_NO_THROW(verifyGraph(layer, cpu));
}
LayerNodes makeLayer(const std::vector<Trim>& trims,
                     bool horizontal,
                     int pitch = 20)
{
  LayerNodes layer;
  layer.layer = "test";
  layer.horizontal = horizontal;
  layer.pitch = pitch;
  layer.rules = makeLayerRules(10);
  layer.nodes = makeVnodes(trims, 0, trims.size(), horizontal);
  return layer;
}
TEST(DelgpuCudaGraph, EmptyAndUnobstructed)
{
  initializeGpu();
  compare(makeLayer({}, true), {});
  compare(makeLayer({{{0, 0, 10, 20}, "test", 1, true}}, true), {});
}
TEST(DelgpuCudaGraph, TouchingBlockedAndHalfDbu)
{
  initializeGpu();
  const std::vector<Trim> trims{{{-5, -10, 5, 10}, "test", 1, true},
                                {{35, -10, 45, 10}, "test", 2, false},
                                {{35, 0, 46, 20}, "test", 3, true},
                                {{0, 10, 10, 30}, "test", 4, false}};
  compare(
      makeLayer(trims, true),
      {{-100, -5, -5, 5}, {35, -5, 200, 5}, {0, 10, 10, 30}, {0, 30, 0, 40}});
}
TEST(DelgpuCudaGraph, RandomizedHorizontalAndVertical)
{
  initializeGpu();
  std::mt19937 rng(20261014);
  for (int trial = 0; trial < 160; ++trial) {
    SCOPED_TRACE(trial);
    const bool horizontal = trial % 2 == 0;
    std::vector<Trim> trims;
    std::vector<odb::Rect> obstacles;
    for (int i = 0; i < 120; ++i) {
      const int s = int(rng() % 2000) - 1000;
      const int t = int(rng() % 12) * 10;
      const int w = 10 + int(rng() % 2);
      const odb::Rect box = horizontal ? odb::Rect(s, t - 10, s + w, t + 10)
                                       : odb::Rect(t - 10, s, t + 10, s + w);
      trims.push_back({box, "test", unsigned(i % 7), bool(rng() % 2)});
    }
    if (trial % 4 != 0) {
      for (int i = 0; i < 150; ++i) {
        const int s = int(rng() % 2400) - 1200;
        const int t = int(rng() % 140) - 10;
        const int len = 1 + int(rng() % 80);
        const int band = 1 + int(rng() % 30);
        obstacles.push_back(horizontal ? odb::Rect(s, t, s + len, t + band)
                                       : odb::Rect(t, s, t + band, s + len));
      }
    }
    compare(makeLayer(trims, horizontal, trial % 5 == 0 ? 0 : 20), obstacles);
  }
}
TEST(DelgpuCudaGraph, WindowAndRuleBoundaries)
{
  initializeGpu();
  for (int delta : {0, 1, 29, 30, 49, 50, 829, 830, 899, 900}) {
    for (int t : {0, 1, 19, 20, 21}) {
      for (bool end : {false, true}) {
        compare(
            makeLayer({{{0, -10, 10, 10}, "test", 1, end},
                       {{delta, t - 10, delta + 10, t + 10}, "test", 2, false}},
                      true),
            {});
      }
    }
  }
}
}  // namespace
}  // namespace delgpu
