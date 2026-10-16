# SPDX-License-Identifier: BSD-3-Clause
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
cc_binary(
    name = "diagnose_conflicts",
    srcs = ["DiagnoseConflicts.cpp"],
    deps = [
        "//src/leo:leo",
        "//src/leo:graph",
        "//src/leo:trim_geometry",
        "//src/odb/src/db",
        "//src/utl",
    ],
)
