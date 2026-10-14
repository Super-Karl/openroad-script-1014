# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

add_executable(TestDelTrims TrimGeometryTest.cpp ../src/graph/TrimGeometry.cpp)
target_include_directories(TestDelTrims PRIVATE ../include)
target_link_libraries(TestDelTrims PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelTrims)
add_dependencies(build_and_test TestDelTrims)

add_executable(TestDelVnodes VnodeTest.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestDelVnodes PRIVATE ../include)
target_link_libraries(TestDelVnodes PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelVnodes)
add_dependencies(build_and_test TestDelVnodes)

add_executable(TestDelArcs ArcTest.cpp
  ../src/graph/Arcs.cpp
  ../src/graph/Domains.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestDelArcs PRIVATE ../include)
target_link_libraries(TestDelArcs PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelArcs)
add_dependencies(build_and_test TestDelArcs)

add_executable(TestDelDomains DomainTest.cpp
  ../src/graph/Domains.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestDelDomains PRIVATE ../include)
target_link_libraries(TestDelDomains PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelDomains)
add_dependencies(build_and_test TestDelDomains)

add_executable(TestDelOptimizer OptimizerTest.cpp
  ../src/optimize/Optimizer.cpp
  ../src/optimize/StallRepair.cpp
)
target_include_directories(TestDelOptimizer PRIVATE ../include)
target_link_libraries(TestDelOptimizer PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelOptimizer)
add_dependencies(build_and_test TestDelOptimizer)

add_executable(TestDelUnrepaired UnrepairedGraphicsTest.cpp ../src/UnrepairedGraphics.cpp)
target_include_directories(TestDelUnrepaired PRIVATE ../include ../src)
target_link_libraries(TestDelUnrepaired PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestDelUnrepaired)
add_dependencies(build_and_test TestDelUnrepaired)

or_integration_tests("del" PASSFAIL_TESTS optimizer_run gui_repeat gui_postprocess)

add_executable(TestDelPostprocess PostprocessTest.cpp ../src/optimize/Postprocess.cpp ../src/graph/Arcs.cpp)
target_include_directories(TestDelPostprocess PRIVATE ../include)
target_link_libraries(TestDelPostprocess PRIVATE odb ortools::ortools GTest::gtest_main)
gtest_discover_tests(TestDelPostprocess)
add_dependencies(build_and_test TestDelPostprocess)
