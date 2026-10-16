# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@cuda_local//:defs.bzl", "CUDA_PATH")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_test")
load(":host_headers.bzl", "host_headers")
load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

# Module CUDA is independent of the optional Kokkos placement backend.
string_flag(
    name = "gpu",
    build_setting_default = "none",
    values = ["none", "cuda"],
)

config_setting(
    name = "gpu_cuda",
    flag_values = {":gpu": "cuda"},
)

GPU_ONLY = select({
    ":gpu_cuda": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

cc_library(
    name = "gpu_api",
    hdrs = ["include/delgpu/Gpu.h", "include/delgpu/Parameters.h", "include/delgpu/GraphCuda.h", "include/delgpu/TrimOptimizer.h", "include/delgpu/Postprocess.h"],
    includes = ["include"],
)

# This entry point uses only host CUDA Runtime APIs, with no device kernels.
# Compile it as C++ in Bazel; retain .cu for the existing nvcc/CMake build.
genrule(
    name = "cuda_translation_unit",
    srcs = ["src/Gpu.cu"],
    outs = ["Gpu.cuda.cpp"],
    cmd = "cp $< $@",
    tags = ["manual"],
    target_compatible_with = GPU_ONLY,
)

cc_library(
    name = "delgpu",
    srcs = [":cuda_translation_unit"],
    additional_compiler_inputs = ["@cuda_local//:compiler_inputs"],
    copts = ["-isystem" + CUDA_PATH + "/include"],
    # System CUDA headers do not provide Bazel module maps.
    features = ["-layering_check"],
    includes = ["include"],
    tags = ["manual"],
    target_compatible_with = GPU_ONLY,
    deps = [
        ":gpu_api",
        "@cuda_local//:cudart",
    ],
)

genrule(
    name = "graph_cuda_translation_unit",
    srcs = ["src/graph/GraphCuda.cu"],
    outs = ["GraphCuda.cuda.cpp"],
    cmd = "cp $< $@",
    tags = ["manual"],
    target_compatible_with = GPU_ONLY,
)

genrule(
    name = "optimizer_cuda_translation_unit",
    srcs = ["src/optimize/TrimOptimizer.cu"],
    outs = ["TrimOptimizer.cuda.cpp"],
    cmd = "cp $< $@",
    tags = ["manual"],
    target_compatible_with = GPU_ONLY,
)

cc_library(
    name = "graph_cuda",
    hdrs = ["src/optimize/Postprocess.cuh", "src/optimize/JointPostprocess.cuh", "src/optimize/EmaPostprocess.cuh"],
    srcs = [":graph_cuda_translation_unit", ":optimizer_cuda_translation_unit"],
    additional_compiler_inputs = [
        "@cuda_local//:compiler_inputs",
        "//bazel/gpu:cuda_placement_new.h",
    ],
    copts = [
        "-xcuda",
        "--cuda-path=" + CUDA_PATH,
        # PTX allows newer devices to JIT this integer-only graph code.
        "--cuda-gpu-arch=sm_75",
        "--cuda-include-ptx=sm_75",
        "-isystem", CUDA_PATH + "/include/cccl",
        "-Wno-unknown-cuda-version",
        "-D_ALLOW_UNSUPPORTED_LIBCPP",
        "-include", "$(location //bazel/gpu:cuda_placement_new.h)",
    ],
    features = ["-layering_check", "-parse_headers"],
    includes = ["include", "src/optimize"],
    tags = ["manual"],
    target_compatible_with = GPU_ONLY,
    deps = [":gpu_api", "@cuda_local//:cudart"],
)

cc_library(
    name = "gpu_unavailable",
    srcs = ["src/GpuUnavailable.cpp", "src/graph/GraphCudaUnavailable.cpp", "src/optimize/TrimOptimizerUnavailable.cpp"],
    deps = [":gpu_api"],
)

# Compile against the host APIs without embedding another OpenDB/Logger/Tcl.
# These symbols are resolved by the OpenROAD executable at runtime.
host_headers(
    name = "host_headers",
    deps = [
        "//:ord",
        "//src/odb/src/db",
        "//src/utl",
        "@boost.stacktrace",
        "@tcl_lang//:tcl",
        "//src/web:core",
        "@boost.geometry",
        "@boost.polygon",
    ],
)

cc_library(
    name = "entry",
    # Compilation-only host dependencies have no direct module map.
    features = ["-layering_check"],
    srcs = [
        "src/DelGpu.cpp",
        "src/GraphGraphics.cpp",
        "src/GraphGraphics.h",
        "src/graph/BuildGraph.cpp",
        "src/graph/BuildGraph.h",
        "src/graph/TrimGeometry.cpp",
        "src/graph/Vnodes.cpp",
        "src/graph/Domains.cpp",
        "src/graph/Arcs.cpp",
        "src/graph/GraphGpu.cpp",
        "src/optimize/GpuAdapter.cpp",
        "src/optimize/Postprocess.cpp",
        "src/optimize/RunOptimization.cpp",
        "src/optimize/RunOptimization.h",
        "src/optimize/Optimizer.cpp",
        "src/optimize/StallRepair.cpp",
    ],
    hdrs = [
        "include/delgpu/DelGpu.h",
        "include/delgpu/AdaptiveLearningRate.h",
        "include/delgpu/Parameters.h",
        "include/delgpu/Graph.h",
        "include/delgpu/GraphGpu.h",
        "include/delgpu/GpuOptimizer.h",
        "include/delgpu/Optimizer.h",
        "include/delgpu/Trim.h",
        "include/delgpu/Rules.h",
    ],
    includes = ["include"],
    deps = [
        ":gpu_api",
        ":host_headers",
    ] + select({
        ":gpu_cuda": [":delgpu", ":graph_cuda"],
        "//conditions:default": [":gpu_unavailable"],
    }),
)

cc_library(
    name = "ui",
    # The shared-library binary has no main() to pull these entry objects.
    alwayslink = True,
    # Compilation-only host dependencies have no direct module map.
    features = ["-layering_check"],
    srcs = [
        "src/MakeDelGpu.cpp",
        ":swig",
        ":tcl",
    ],
    hdrs = ["include/delgpu/MakeDelGpu.h"],
    includes = ["include"],
    deps = [
        ":entry",
        ":gpu_api",
        ":host_headers",
    ],
)

# ELF shared module; host API references deliberately remain unresolved.
cc_binary(
    name = "libdelgpu.so",
    linkopts = ["-Wl,-z,undefs"],
    linkshared = True,
    linkstatic = True,
    deps = [":ui"],
)

# The host sees only the public API and links the generated shared module.
cc_import(
    name = "module",
    hdrs = [
        "include/delgpu/DelGpu.h",
        "include/delgpu/Parameters.h",
        "include/delgpu/MakeDelGpu.h",
    ],
    includes = ["include"],
    shared_library = ":libdelgpu.so",
    deps = ["@tcl_lang//:tcl"],
)

tcl_encode(
    name = "tcl",
    srcs = ["src/delgpu.tcl"],
    char_array_name = "delgpu_tcl_inits",
    namespace = "delgpu",
)

tcl_wrap_cc(
    name = "swig",
    srcs = ["src/delgpu.i", "//:error_swig"],
    module = "delgpu",
    namespace_prefix = "delgpu",
    root_swig_src = "src/delgpu.i",
    swig_includes = ["..", "src"],
)

py_test(
    name = "cuda_discovery_test",
    size = "small",
    srcs = ["test/cuda_discovery_test.py"],
    args = ["$(location scripts/build.sh)"],
    data = ["scripts/build.sh"],
)

exports_files(["README.md"])

cc_test(
    name = "graph_gpu_test",
    srcs = [
        "test/GraphGpuTest.cpp",
        "test/GpuOptimizerTest.cpp",
        "test/PostprocessTest.cpp",
        "test/OptimizerTest.cpp",
        "test/TrimGeometryTest.cpp",
        "test/DomainTest.cpp",
        "test/ArcTest.cpp",
        "test/VnodeTest.cpp",
    ],
    features = ["-layering_check"],
    tags = ["manual", "requires-gpu"],
    target_compatible_with = GPU_ONLY,
    deps = [
        ":entry",
        ":gpu_api",
        "//src/odb/src/db",
        "@googletest//:gtest_main",
    ],
)
