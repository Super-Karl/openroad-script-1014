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
    hdrs = ["include/leo/Trim.h"],
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
        "include/leo/Graph.h",
        "include/leo/Rules.h",
    ],
    includes = ["include"],
    deps = [
        ":trim_geometry",
        "//src/odb/src/db",
        "@boost.geometry",
    ],
)

cc_library(
    name = "leo",
    srcs = [
        "src/ComponentModel.h",
        "src/Leo.cpp",
        "src/Measurement.cpp",
        "src/ema/Ema.cpp",
        "src/ema/TightTree.cpp",
        "src/ema/TightTree.h",
        "src/graph/BuildGraph.cpp",
        "src/graph/Components.cpp",
        "src/lpa/DifferenceConstraints.cpp",
        "src/lpa/Lpa.cpp",
    ],
    hdrs = [
        "include/leo/Ema.h",
        "include/leo/Leo.h",
        "include/leo/Lpa.h",
        "include/leo/Measurement.h",
        "src/lpa/DifferenceConstraints.h",
    ],
    includes = ["include", "src"],
    deps = [
        ":graph",
        ":trim_geometry",
        "//:ord",
        "//src/odb/src/db",
        "//src/utl",
    ],
)

cc_library(
    name = "ui",
    srcs = [
        "src/GraphGraphics.cpp",
        "src/GraphGraphics.h",
        "src/MakeLeo.cpp",
        "src/TrimGraphics.cpp",
        "src/TrimGraphics.h",
        ":swig",
        ":tcl",
    ],
    hdrs = ["include/leo/MakeLeo.h"],
    includes = ["include", "src"],
    deps = [
        ":graph",
        ":leo",
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
    srcs = ["src/leo.tcl"],
    char_array_name = "leo_tcl_inits",
    namespace = "leo",
)

tcl_wrap_cc(
    name = "swig",
    srcs = [
        "src/leo.i",
        "//:error_swig",
    ],
    module = "leo",
    namespace_prefix = "leo",
    root_swig_src = "src/leo.i",
    swig_includes = ["src"],
)

exports_files(["README.md"])
