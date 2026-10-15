# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

# Load the initial routed design, then inspect the GPU and current OpenDB block.
set delgpu_script_dir [file dirname [file normalize [info script]]]
set delgpu_orfs_root [file normalize [file join $delgpu_script_dir ../../../../..]]
set delgpu_route_db [file join $delgpu_orfs_root flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {
  set delgpu_route_db [file normalize $::env(DELGPU_ROUTE_DB)]
}
if {![file isfile $delgpu_route_db]} {
  error "DELGPU design database not found: $delgpu_route_db"
}
puts "DELGPU read design: $delgpu_route_db"
read_db $delgpu_route_db
set delgpu_options [list -max_iterations 0]
foreach {env_key option} {DELGPU_GUI -gui DELGPU_VERIFY -verify DELGPU_BACKEND -backend DELGPU_ITERATIONS -max_iterations DELGPU_MAX_STEP -max_step DELGPU_JUMP_OFFSET_NM -jump_offset_nm DELGPU_MAX_BACKTRACKS -max_backtracks DELGPU_LEARNING_RATE -learning_rate DELGPU_LEARNING_RATE_END -learning_rate_end DELGPU_LEARNING_RATE_MODE -learning_rate_mode DELGPU_POSTPROCESS -postprocess DELGPU_POST_SWEEPS -post_sweeps DELGPU_POST_PAIR_ROUNDS -post_pair_rounds DELGPU_POST_RETRACT_SWEEPS -post_retract_sweeps DELGPU_FRAME_MS -frame_ms DELGPU_REPORT -report} {
  if {[info exists ::env($env_key)]} {
    lappend delgpu_options $option $::env($env_key)
  }
}
delgpu {*}$delgpu_options
