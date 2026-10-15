# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "trim_geometry",
    srcs = ["src/graph/TrimGeometry.cpp"],
    hdrs = ["include/del/Trim.h"],
    includes = ["include"],
    deps = ["//src/odb/src/db", "@boost.polygon"],
)

cc_library(
    name = "graph",
    srcs = [
        "src/graph/Arcs.cpp",
        "src/graph/Domains.cpp",
        "src/graph/Vnodes.cpp",
    ],
    hdrs = [
        "include/del/Graph.h",
        "include/del/Rules.h",
    ],
    includes = ["include"],
    deps = [
        ":trim_geometry",
        "//src/odb/src/db",
        "@boost.geometry",
    ],
)

cc_library(
    name = "optimizer",
    srcs = ["src/optimize/Optimizer.cpp", "src/optimize/StallRepair.cpp"],
    hdrs = ["include/del/Optimizer.h"],
    includes = ["include"],
    deps = [":graph"],
)

cc_library(
    name = "postprocess",
    srcs = ["src/optimize/Postprocess.cpp"],
    hdrs = ["include/del/Postprocess.h"],
    includes = ["include"],
    deps = [":graph", "@or-tools//ortools/linear_solver", "@or-tools//ortools/linear_solver:linear_solver_base", "@or-tools//ortools/linear_solver:solve_mp_model"],
)

cc_library(
    name = "del",
    srcs = [
        "src/Del.cpp",
        "src/optimize/RunOptimization.cpp",
        "src/optimize/ResidualReport.cpp",
        "src/optimize/ResidualReport.h",
        "src/graph/BuildGraph.cpp",
    ],
    hdrs = ["include/del/Del.h"],
    includes = ["include"],
    deps = [
        ":graph",
        ":optimizer",
        ":postprocess",
        ":trim_geometry",
        "//:ord",
        "//src/odb/src/db",
        "//src/utl",
    ],
)

cc_library(
    name = "unrepaired_bounds",
    srcs = ["src/UnrepairedGraphics.cpp"],
    hdrs = ["src/UnrepairedGraphics.h"],
    includes = ["src"],
    deps = [":graph", ":optimizer", "//src/odb/src/db"],
)

cc_library(
    name = "ui",
    srcs = [
        "src/OptimizationGraphics.cpp",
        "src/OptimizationGraphics.h",
        "src/GraphGraphics.cpp",
        "src/GraphGraphics.h",
        "src/MakeDel.cpp",
        "src/TrimGraphics.cpp",
        "src/TrimGraphics.h",
        ":swig",
        ":tcl",
    ],
    hdrs = ["include/del/MakeDel.h"],
    includes = ["include", "src"],
    deps = [
        ":graph",
        ":del",
        ":optimizer",
        ":unrepaired_bounds",
        ":trim_geometry",
        "//:ord",
        "//src/web:core",
        "//src/odb/src/db",
        "//src/utl",
        "@boost.stacktrace",
        "@tcl_lang//:tcl",
    ],
)

tcl_encode(
    name = "tcl",
    srcs = ["src/del.tcl"],
    char_array_name = "del_tcl_inits",
    namespace = "del",
)

tcl_wrap_cc(
    name = "swig",
    srcs = [
        "src/del.i",
        "//:error_swig",
    ],
    module = "del",
    namespace_prefix = "del",
    root_swig_src = "src/del.i",
    swig_includes = ["src"],
)

exports_files(["README.md"])
