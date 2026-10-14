# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@cuda_local//:defs.bzl", "CUDA_PATH")
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
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
    hdrs = ["include/delgpu/Gpu.h"],
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

cc_library(
    name = "gpu_unavailable",
    srcs = ["src/GpuUnavailable.cpp"],
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
    ],
)

cc_library(
    name = "entry",
    # Compilation-only host dependencies have no direct module map.
    features = ["-layering_check"],
    srcs = ["src/DelGpu.cpp"],
    hdrs = ["include/delgpu/DelGpu.h"],
    includes = ["include"],
    deps = [
        ":gpu_api",
        ":host_headers",
    ] + select({
        ":gpu_cuda": [":delgpu"],
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

exports_files(["README.md"])
