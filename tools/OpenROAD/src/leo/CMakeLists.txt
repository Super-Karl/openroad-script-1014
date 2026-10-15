# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

include("openroad")

swig_lib(NAME      leo
         NAMESPACE leo
         I_FILE    src/leo.i
         SCRIPTS   src/leo.tcl
)

add_library(leo_lib SHARED
  src/Leo.cpp
  src/Measurement.cpp
  src/graph/Arcs.cpp
  src/graph/BuildGraph.cpp
  src/graph/Components.cpp
  src/graph/Domains.cpp
  src/graph/TrimGeometry.cpp
  src/graph/Vnodes.cpp
  src/lpa/DifferenceConstraints.cpp
  src/lpa/Lpa.cpp
  src/ema/Ema.cpp
  src/ema/TightTree.cpp
)

target_include_directories(leo_lib
  PUBLIC
    include
  PRIVATE
    ${OPENROAD_HOME}/include
    ${ODB_HOME}/include
)

target_link_libraries(leo_lib
  PRIVATE
    utl_lib
)

# Keep the algorithm implementation in a shared library so changes to the
# LEO stages do not require relinking the large OpenROAD executable.
set_target_properties(leo_lib PROPERTIES
  OUTPUT_NAME leo
  INSTALL_RPATH "$ORIGIN"
)

install(TARGETS leo_lib DESTINATION lib)

target_sources(leo
  PRIVATE
    src/GraphGraphics.cpp
    src/MakeLeo.cpp
    src/TrimGraphics.cpp
)

target_include_directories(leo
  PUBLIC
    include
  PRIVATE
    src
)

target_link_libraries(leo
  PRIVATE
    ${TCL_LIBRARY}
    leo_lib
    gui_core
    odb
    utl_lib
)

if(ENABLE_TESTS)
  add_subdirectory(test)
endif()
