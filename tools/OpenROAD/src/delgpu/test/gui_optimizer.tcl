# SPDX-License-Identifier: BSD-3-Clause
set ::env(DELGPU_GUI) 1
set ::env(DELGPU_VERIFY) 0
set ::env(DELGPU_ITERATIONS) 0
set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir ../scripts/open_route.tcl]
set image_dir [file normalize [file join $script_dir ../../../../../flow/reports/asap7/ibex/base]]
if {[info exists ::env(DELGPU_IMAGE_DIR)]} { set image_dir $::env(DELGPU_IMAGE_DIR) }
file mkdir $image_dir
proc preview_controls {} {
  gui::set_display_controls "Layers/*" visible false
  gui::set_display_controls "Instances/*" visible false
  gui::set_display_controls "DELGPU/*" visible false
  gui::set_display_controls "DELGPU/M2/*" visible true
  gui::set_display_controls "DELGPU/M2/domains" visible false
}
preview_controls
gui::save_image [file join $image_dir delgpu_opt_before.png] 20 20 24 24 1200
delgpu -gui 1 -max_iterations 20 -frame_ms 100 -wire_weight 0.1 -post_retract_mode ema_lpa -post_joint_rounds 0
preview_controls
gui::save_image [file join $image_dir delgpu_opt_after.png] 20 20 24 24 1200
proc bytes {p} { set f [open $p rb]; set b [read $f]; close $f; return $b }
if {[bytes [file join $image_dir delgpu_opt_before.png]] eq [bytes [file join $image_dir delgpu_opt_after.png]]} {error "Iteration did not update displayed coordinates"}
delgpu -gui 0 -max_iterations 0
gui::save_image [file join $image_dir delgpu_opt_hidden.png] 20 20 24 24 1200
if {[bytes [file join $image_dir delgpu_opt_after.png]] eq [bytes [file join $image_dir delgpu_opt_hidden.png]]} {error "Disabling display left stale iteration overlay"}
puts "pass: GPU iteration display updates and GUI switch clears overlay"
