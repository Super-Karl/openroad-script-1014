# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

sta::define_cmd_args "delgpu" {[-gui boolean] [-verify boolean] [-backend gpu|cpu] [-max_iterations iterations] [-max_step width_multiple] [-jump_offset_nm nanometers] [-max_backtracks retries] [-learning_rate scale] [-learning_rate_end scale] [-learning_rate_mode linear|adaptive] [-postprocess boolean] [-post_sweeps rounds] [-post_pair_rounds rounds] [-post_retract_sweeps rounds] [-post_retract_mode single|cooperative|ema|ema_jump|ema_lpa] [-post_joint_rounds rounds] [-post_joint_trials candidates] [-frame_ms milliseconds] [-report csv_file]}

proc delgpu { args } {
  sta::parse_key_args "delgpu" args keys {-gui -verify -backend -max_iterations -max_step -jump_offset_nm -max_backtracks -learning_rate -learning_rate_end -learning_rate_mode -postprocess -post_sweeps -post_pair_rounds -post_retract_sweeps -post_retract_mode -post_joint_rounds -post_joint_trials -frame_ms -report} flags {}
  sta::check_argc_eq0 "delgpu" $args
  set postprocess 1
  set show 0
  set verify 0
  set backend gpu
  foreach {key var} {-gui show -verify verify -postprocess postprocess} {
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
  set iterations 150
  set max_step 0.25
  set jump_offset_nm 35
  set max_backtracks 0
  set learning_rate 1.5
  set learning_rate_mode adaptive
  if {$backend eq "cpu"} {
    set learning_rate 1.0
    set learning_rate_mode linear
  }
  if {$backend eq "cpu"} {
    if {[info exists keys(-postprocess)] && $postprocess} {
      error "delgpu: -postprocess 1 requires -backend gpu"
    }
    set postprocess 0
  }
  set post_sweeps 24
  set post_pair_rounds 8
  set post_retract_sweeps 8
  set post_retract_mode cooperative
  set post_joint_rounds 0
  set post_joint_trials 16
  set frame_ms 100
  set report ""
  foreach {key var} {-max_iterations iterations -max_step max_step -jump_offset_nm jump_offset_nm -max_backtracks max_backtracks -learning_rate learning_rate -learning_rate_mode learning_rate_mode -post_sweeps post_sweeps -post_pair_rounds post_pair_rounds -post_retract_sweeps post_retract_sweeps -post_retract_mode post_retract_mode -post_joint_rounds post_joint_rounds -post_joint_trials post_joint_trials -frame_ms frame_ms -report report} {
    if {[info exists keys($key)]} { set $var $keys($key) }
  }
  if {![string is integer -strict $post_joint_rounds] || $post_joint_rounds < 0 || $post_joint_rounds > 64} {
    error "delgpu: -post_joint_rounds requires an integer from 0 to 64"
  }
  if {![string is integer -strict $post_joint_trials] || $post_joint_trials < 2 || $post_joint_trials > 64} {
    error "delgpu: -post_joint_trials requires an integer from 2 to 64"
  }
  if {$backend eq "cpu" && $post_joint_rounds > 0} {
    error "delgpu: joint search requires -backend gpu"
  }
  if {$post_joint_rounds > 0 && !$postprocess} {
    error "delgpu: joint search requires -postprocess 1"
  }
  if {$post_retract_mode ni {single cooperative ema ema_jump ema_lpa}} {
    error "delgpu: -post_retract_mode must be single, cooperative, ema, ema_jump or ema_lpa"
  }
  if {$learning_rate_mode ni {linear adaptive}} {
    error "delgpu: -learning_rate_mode must be linear or adaptive"
  }
  set learning_rate_end $learning_rate
  if {[info exists keys(-learning_rate_end)]} { set learning_rate_end $keys(-learning_rate_end) }
  if {![string is integer -strict $max_backtracks] || $max_backtracks < 0 || $max_backtracks > 19} {
    error "delgpu: -max_backtracks requires an integer from 0 to 19 (0 disables step halving)"
  }
  if {$backend eq "cpu" && [info exists keys(-max_backtracks)]} {
    error "delgpu: -max_backtracks controls only GPU; the CPU reference keeps 19 retries"
  }
  if {$backend eq "cpu" && ([info exists keys(-learning_rate)] || [info exists keys(-learning_rate_end)] || [info exists keys(-learning_rate_mode)])} {
    error "delgpu: -learning_rate controls only GPU; CPU reference uses 1.0"
  }
  foreach {key value} [list -max_iterations $iterations -frame_ms $frame_ms -post_sweeps $post_sweeps -post_pair_rounds $post_pair_rounds -post_retract_sweeps $post_retract_sweeps] {
    if {![string is integer -strict $value] || $value < 0 || $value > 2147483647} {
      error "delgpu: $key requires a nonnegative 32-bit integer"
    }
  }
  foreach {key value} [list -max_step $max_step -jump_offset_nm $jump_offset_nm -learning_rate $learning_rate -learning_rate_end $learning_rate_end] {
    if {![string is double -strict $value] || !($value > 0 && $value < Inf)} {
      error "delgpu: $key requires a finite positive number"
    }
  }
  if {$learning_rate_mode eq "adaptive"} {
    if {![info exists keys(-learning_rate_end)]} { set learning_rate_end [expr {min($learning_rate, 0.5)}] }
    if {$learning_rate_end > $learning_rate} { error "delgpu: adaptive learning_rate_end must not exceed learning_rate" }
  }
  delgpu::run_cmd $show $verify [expr {$backend eq "gpu"}] $iterations $max_step $jump_offset_nm $frame_ms $report $max_backtracks $learning_rate $learning_rate_end $learning_rate_mode $postprocess $post_sweeps $post_pair_rounds $post_retract_sweeps $post_retract_mode $post_joint_rounds $post_joint_trials
}
