# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "leo" {[-prune_factor k] [-report csv_file]}

proc leo { args } {
  sta::parse_key_args "leo" args keys {-report -prune_factor} flags {}
  sta::check_argc_eq0 "leo" $args
  set report ""
  if {[info exists keys(-report)]} { set report $keys(-report) }
  set prune_factor 3
  if {[info exists keys(-prune_factor)]} { set prune_factor $keys(-prune_factor) }
  if {![string is integer -strict $prune_factor] || $prune_factor <= 0 || $prune_factor > 2147483647} {
    error "leo: -prune_factor must be a positive 32-bit integer"
  }
  leo::run_cmd $report $prune_factor
}
