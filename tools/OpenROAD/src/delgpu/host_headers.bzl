# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

"""Compilation-only access to APIs supplied by the OpenROAD host."""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _host_headers_impl(ctx):
    merged = cc_common.merge_cc_infos(cc_infos = [dep[CcInfo] for dep in ctx.attr.deps])
    return [CcInfo(compilation_context = merged.compilation_context)]

host_headers = rule(
    implementation = _host_headers_impl,
    doc = "Expose host compilation contexts without linking host libraries.",
    attrs = {
        "deps": attr.label_list(providers = [CcInfo]),
    },
)
