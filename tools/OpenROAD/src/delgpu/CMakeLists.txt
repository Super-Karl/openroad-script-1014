# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

cmake_minimum_required(VERSION 3.18)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(delgpu LANGUAGES CXX)
  set(DELGPU_STANDALONE ON)
  option(ENABLE_DELGPU_CUDA "Enable native CUDA kernels for DELGPU" ON)
else()
  set(DELGPU_STANDALONE OFF)
  include("openroad")
endif()

if(ENABLE_DELGPU_CUDA)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 80)
  endif()
  if(DELGPU_STANDALONE)
    enable_language(CUDA)
  endif()
  find_package(CUDAToolkit REQUIRED)
endif()

# Keep the CUDA library independent of the OpenROAD/Tcl entry point.
add_library(delgpu_lib STATIC)
target_include_directories(delgpu_lib PUBLIC include)
set_target_properties(delgpu_lib PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CUDA_STANDARD 17
  CUDA_STANDARD_REQUIRED ON
  CUDA_RUNTIME_LIBRARY Static
  POSITION_INDEPENDENT_CODE ON
  INTERPROCEDURAL_OPTIMIZATION FALSE
)

if(ENABLE_DELGPU_CUDA)
  target_sources(delgpu_lib PRIVATE src/Gpu.cu src/graph/GraphCuda.cu src/optimize/TrimOptimizer.cu)
  target_link_libraries(delgpu_lib PRIVATE CUDA::cudart_static)
  # Inherited OpenROAD warning options target the host C++ compiler, not nvcc.
  get_target_property(DELGPU_HOST_OPTIONS delgpu_lib COMPILE_OPTIONS)
  if(DELGPU_HOST_OPTIONS)
    set_property(TARGET delgpu_lib PROPERTY COMPILE_OPTIONS
      "$<$<COMPILE_LANGUAGE:CXX>:${DELGPU_HOST_OPTIONS}>")
  endif()
else()
  target_sources(delgpu_lib PRIVATE src/GpuUnavailable.cpp src/graph/GraphCudaUnavailable.cpp src/optimize/TrimOptimizerUnavailable.cpp)
endif()

if(NOT DELGPU_STANDALONE)
  swig_lib(NAME      delgpu
           TYPE      SHARED
           NAMESPACE delgpu
           I_FILE    src/delgpu.i
           SWIG_INCLUDES ${OPENROAD_HOME}/src
           SCRIPTS   src/delgpu.tcl
  )
  target_sources(delgpu PRIVATE src/MakeDelGpu.cpp src/DelGpu.cpp src/GraphGraphics.cpp
    src/graph/BuildGraph.cpp src/graph/TrimGeometry.cpp src/graph/Vnodes.cpp
    src/graph/Domains.cpp src/graph/Arcs.cpp src/graph/GraphGpu.cpp src/optimize/GpuAdapter.cpp src/optimize/Postprocess.cpp src/optimize/RunOptimization.cpp
    src/optimize/Optimizer.cpp src/optimize/StallRepair.cpp)
  target_include_directories(delgpu
    PUBLIC include
    PRIVATE ${ODB_HOME}/include
  )
  target_link_libraries(delgpu PRIVATE
    ${TCL_LIBRARY} delgpu_lib utl_lib gui_core
    INTERFACE odb)
  # Keep the implementation and embedded Tcl together so only this library
  # needs rebuilding during development. OpenROAD exports the host symbols
  # referenced by the SWIG bridge (OpenRoad::openRoad(), getLogger(), etc.).
  set_target_properties(delgpu PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME delgpu
    INSTALL_RPATH "$ORIGIN"
  )
  if(APPLE)
    target_link_options(delgpu PRIVATE "LINKER:-undefined,dynamic_lookup")
  endif()
  install(TARGETS delgpu LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
  if(ENABLE_TESTS AND ENABLE_DELGPU_CUDA)
    add_subdirectory(test)
  endif()
endif()
