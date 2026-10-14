# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

include("openroad")
find_package(ortools REQUIRED)

swig_lib(NAME      del
         TYPE      OBJECT
         NAMESPACE del
         I_FILE    src/del.i
         SCRIPTS   src/del.tcl
)

# Compile the Tcl/GUI bridge once, then include its objects in libdel.so.
# Keep the SWIG target name "del" so Del_Init and del_tcl_inits stay unchanged.
set_target_properties(del PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(del_lib SHARED
  $<TARGET_OBJECTS:del>
  src/Del.cpp
  src/optimize/Optimizer.cpp
  src/optimize/StallRepair.cpp
  src/optimize/RunOptimization.cpp
  src/optimize/ResidualReport.cpp
  src/optimize/Postprocess.cpp
  src/graph/Arcs.cpp
  src/graph/BuildGraph.cpp
  src/graph/Domains.cpp
  src/graph/TrimGeometry.cpp
  src/graph/Vnodes.cpp
)

target_include_directories(del_lib
  PUBLIC
    include
  PRIVATE
    ${OPENROAD_HOME}/include
    ${ODB_HOME}/include
)

target_link_libraries(del_lib
  PRIVATE
    ${TCL_LIBRARY}
    utl_lib
    ortools::ortools
  INTERFACE
    gui_core
    odb
)

# The algorithm, GUI renderers and Tcl bindings all live in this shared library.
# OpenROAD exports the web/ODB symbols used here. Keep those static libraries
# on the executable's link line instead of copying them into libdel.so.
set_target_properties(del_lib PROPERTIES
  OUTPUT_NAME del
  INSTALL_RPATH "$ORIGIN"
)

install(TARGETS del_lib DESTINATION lib)

target_sources(del
  PRIVATE
    src/OptimizationGraphics.cpp
    src/UnrepairedGraphics.cpp
    src/GraphGraphics.cpp
    src/MakeDel.cpp
    src/TrimGraphics.cpp
)

target_include_directories(del
  PUBLIC
    include
  PRIVATE
    src
)

target_link_libraries(del
  PRIVATE
    ${TCL_LIBRARY}
    gui_core
    odb
    utl_lib
)

if(ENABLE_TESTS)
  add_subdirectory(test)
endif()
