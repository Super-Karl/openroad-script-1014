# SPDX-License-Identifier: BSD-3-Clause
add_executable(TestDelgpuGraph
  GraphGpuTest.cpp GpuOptimizerTest.cpp PostprocessTest.cpp OptimizerTest.cpp TrimGeometryTest.cpp DomainTest.cpp ArcTest.cpp VnodeTest.cpp
  ../src/graph/TrimGeometry.cpp ../src/graph/Vnodes.cpp
  ../src/graph/Domains.cpp ../src/graph/Arcs.cpp ../src/graph/GraphGpu.cpp ../src/optimize/GpuAdapter.cpp
  ../src/optimize/Postprocess.cpp ../src/optimize/Optimizer.cpp ../src/optimize/StallRepair.cpp
)
target_include_directories(TestDelgpuGraph PRIVATE ../include)
target_link_libraries(TestDelgpuGraph PRIVATE odb delgpu_lib GTest::gtest_main)
gtest_discover_tests(TestDelgpuGraph)
add_dependencies(build_and_test TestDelgpuGraph)
