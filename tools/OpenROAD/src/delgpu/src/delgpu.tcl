# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "delgpu" {[-gui boolean] [-verify boolean] [-backend gpu|cpu] [-max_iterations iterations] [-max_step width_multiple] [-jump_offset_nm nanometers] [-frame_ms milliseconds] [-report csv_file]}

proc delgpu { args } {
  sta::parse_key_args "delgpu" args keys {-gui -verify -backend -max_iterations -max_step -jump_offset_nm -frame_ms -report} flags {}
  sta::check_argc_eq0 "delgpu" $args
  set show [gui::enabled]
  set verify 0
  set backend gpu
  foreach {key var} {-gui show -verify verify} {
    if {[info exists keys($key)]} {
      if {![string is boolean -strict $keys($key)]} {
        error "delgpu: $key requires a boolean (0 or 1)"
      }
      set $var [expr {$keys($key) ? 1 : 0}]
    }
  }
  if {[info exists keys(-backend)]} { set backend $keys(-backend) }
  if {$backend ni {gpu cpu}} { error "delgpu: -backend must be gpu or cpu" }
  if {$verify && $backend eq "cpu"} {
    error "delgpu: -verify 1 requires -backend gpu"
  }
  set iterations 200
  set max_step 0.25
  set jump_offset_nm 35
  set frame_ms 100
  set report ""
  foreach {key var} {-max_iterations iterations -max_step max_step -jump_offset_nm jump_offset_nm -frame_ms frame_ms -report report} {
    if {[info exists keys($key)]} { set $var $keys($key) }
  }
  foreach {key value} [list -max_iterations $iterations -frame_ms $frame_ms] {
    if {![string is integer -strict $value] || $value < 0 || $value > 2147483647} {
      error "delgpu: $key requires a nonnegative 32-bit integer"
    }
  }
  foreach {key value} [list -max_step $max_step -jump_offset_nm $jump_offset_nm] {
    if {![string is double -strict $value] || !($value > 0 && $value < Inf)} {
      error "delgpu: $key requires a finite positive number"
    }
  }
  delgpu::run_cmd $show $verify [expr {$backend eq "gpu"}] $iterations $max_step $jump_offset_nm $frame_ms $report
}
