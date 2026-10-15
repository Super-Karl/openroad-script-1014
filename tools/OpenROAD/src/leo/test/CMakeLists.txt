# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

add_executable(TestLeoTrims TrimGeometryTest.cpp ../src/graph/TrimGeometry.cpp)
target_include_directories(TestLeoTrims PRIVATE ../include)
target_link_libraries(TestLeoTrims PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestLeoTrims)
add_dependencies(build_and_test TestLeoTrims)

add_executable(TestLeoVnodes VnodeTest.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestLeoVnodes PRIVATE ../include)
target_link_libraries(TestLeoVnodes PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestLeoVnodes)
add_dependencies(build_and_test TestLeoVnodes)

add_executable(TestLeoArcs ArcTest.cpp
  ../src/graph/Arcs.cpp
  ../src/graph/Domains.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestLeoArcs PRIVATE ../include)
target_link_libraries(TestLeoArcs PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestLeoArcs)
add_dependencies(build_and_test TestLeoArcs)

add_executable(TestLeoDomains DomainTest.cpp
  ../src/graph/Arcs.cpp
  ../src/graph/Domains.cpp
  ../src/graph/TrimGeometry.cpp
  ../src/graph/Vnodes.cpp
)
target_include_directories(TestLeoDomains PRIVATE ../include)
target_link_libraries(TestLeoDomains PRIVATE odb GTest::gtest_main)
gtest_discover_tests(TestLeoDomains)
add_dependencies(build_and_test TestLeoDomains)

add_executable(TestLeoLpa LpaTest.cpp
  ../src/graph/Arcs.cpp
  ../src/graph/Components.cpp
  ../src/lpa/DifferenceConstraints.cpp
  ../src/lpa/Lpa.cpp
)
target_include_directories(TestLeoLpa PRIVATE ../include ../src)
target_link_libraries(TestLeoLpa PRIVATE odb utl_lib GTest::gtest_main)
gtest_discover_tests(TestLeoLpa)
add_dependencies(build_and_test TestLeoLpa)

add_executable(TestLeoEma EmaTest.cpp
  ../src/ema/Ema.cpp
  ../src/ema/TightTree.cpp
  ../src/graph/Arcs.cpp
  ../src/graph/Components.cpp
  ../src/lpa/DifferenceConstraints.cpp
  ../src/lpa/Lpa.cpp
)
target_include_directories(TestLeoEma PRIVATE ../include ../src)
target_link_libraries(TestLeoEma PRIVATE odb utl_lib GTest::gtest_main)
gtest_discover_tests(TestLeoEma)
add_dependencies(build_and_test TestLeoEma)

add_executable(TestLeoMeasurement MeasurementTest.cpp
  ../src/Measurement.cpp
  ../src/ema/Ema.cpp ../src/ema/TightTree.cpp
  ../src/graph/Arcs.cpp ../src/graph/Components.cpp
  ../src/lpa/DifferenceConstraints.cpp ../src/lpa/Lpa.cpp
)
target_include_directories(TestLeoMeasurement PRIVATE ../include ../src)
target_link_libraries(TestLeoMeasurement PRIVATE odb utl_lib GTest::gtest_main)
gtest_discover_tests(TestLeoMeasurement)
add_dependencies(build_and_test TestLeoMeasurement)
