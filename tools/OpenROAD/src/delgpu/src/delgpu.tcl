# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "delgpu" {}

proc delgpu { args } {
  sta::parse_key_args "delgpu" args keys {} flags {}
  sta::check_argc_eq0 "delgpu" $args
  delgpu::run_cmd
}
