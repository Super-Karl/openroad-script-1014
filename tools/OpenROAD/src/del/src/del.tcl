# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

# Persistent global defaults, backed by del::global_parameters in C++.
sta::define_cmd_args "del_configure" {[-max_iterations iterations] [-max_step width_multiple] [-jump_offset_nm nanometers]}
proc del_configure { args } {
  sta::parse_key_args "del_configure" args keys {-max_iterations -max_step -jump_offset_nm} flags {}
  sta::check_argc_eq0 "del_configure" $args
  set iterations [del::configured_iterations_cmd]
  set max_step [del::configured_max_step_cmd]
  set jump_offset [del::configured_jump_offset_cmd]
  if {[info exists keys(-max_iterations)]} { set iterations $keys(-max_iterations) }
  if {[info exists keys(-max_step)]} { set max_step $keys(-max_step) }
  if {[info exists keys(-jump_offset_nm)]} { set jump_offset $keys(-jump_offset_nm) }
  if {![string is integer -strict $iterations] || $iterations < 1 || $iterations > 2147483647} {
    error "-max_iterations must be an integer from 1 to 2147483647"
  }
  # C++ validates finite positive step/offset and commits all three atomically.
  del::configure_cmd $iterations $max_step $jump_offset
  puts "DEL global parameters: max_iterations=$iterations max_step=${max_step}W jump_offset_nm=$jump_offset"
  return [dict create max_iterations $iterations max_step $max_step jump_offset_nm $jump_offset]
}

sta::define_cmd_args "del" {[-max_iterations iterations] [-max_step width_multiple] [-jump_offset_nm nanometers] [-frame_ms milliseconds] [-report csv_file] [-graph_only] [-step]}
proc del { args } {
  sta::parse_key_args "del" args keys {-max_iterations -max_step -jump_offset_nm -frame_ms -report} flags {-graph_only -step}
  sta::check_argc_eq0 "del" $args
  set frame_ms 50
  set report ""
  if {[info exists keys(-frame_ms)]} { set frame_ms $keys(-frame_ms) }
  if {![string is integer -strict $frame_ms] || $frame_ms < 0 || $frame_ms > 10000} {
    error "-frame_ms must be an integer from 0 to 10000"
  }
  if {[info exists keys(-report)]} { set report $keys(-report) }
  set step_mode [info exists flags(-step)]
  if {$step_mode && [info exists flags(-graph_only)]} {
    error "-step and -graph_only cannot be combined"
  }
  set options {}
  foreach key {-max_iterations -max_step -jump_offset_nm} {
    if {[info exists keys($key)]} { lappend options $key $keys($key) }
  }
  del_configure {*}$options
  set iterations [del::configured_iterations_cmd]
  if {[info exists flags(-graph_only)]} { set iterations 0 }
  del::run_cmd $iterations $frame_ms $report $step_mode
}

sta::define_cmd_args "n" {}
proc n { args } {
  sta::check_argc_eq0 "n" $args
  del::next_cmd
}

sta::define_cmd_args "c" {}
proc c { args } {
  sta::check_argc_eq0 "c" $args
  del::continue_cmd
}
