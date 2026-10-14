# SPDX-License-Identifier: BSD-3-Clause
# Run with: QT_QPA_PLATFORM=offscreen openroad -gui -no_init -exit gui_graph.tcl
set test_dir [file dirname [file normalize [info script]]]
set ::env(DELGPU_GUI) 1
set ::env(DELGPU_VERIFY) 1
source [file join $test_dir ../scripts/open_route.tcl]
if {![gui::enabled]} { error "GUI backend is not active" }
set output_dir [file normalize [file join $test_dir ../../../../../flow/reports/asap7/ibex/base]]
# Prefer a caller-selected artifact directory for repeatable regression runs.
if {[info exists ::env(DELGPU_IMAGE_DIR)]} { set output_dir $::env(DELGPU_IMAGE_DIR) }
file mkdir $output_dir
proc image_bytes {path} {
  set f [open $path rb]
  set bytes [read $f]
  close $f
  return $bytes
}
set enabled [file join $output_dir delgpu_graph.png]
set disabled [file join $output_dir delgpu_graph_disabled.png]
proc configure_preview {} {
  gui::set_display_controls "Layers/*" visible false
  gui::set_display_controls "Instances/*" visible false
  gui::set_display_controls "DELGPU/*" visible false
  gui::set_display_controls "DELGPU/M2/*" visible true
  gui::set_display_controls "DELGPU/M2/domains" visible false
}
configure_preview
gui::fit
gui::save_image $enabled 20 20 24 24 1200
delgpu -max_iterations 0 -gui 0
gui::save_image $disabled 20 20 24 24 1200
if {[image_bytes $enabled] eq [image_bytes $disabled]} {
  error "Disabling graph display did not remove the overlay"
}
delgpu -max_iterations 0 -gui 1
configure_preview
gui::save_image [file join $output_dir delgpu_graph_repeated.png] 20 20 24 24 1200
if {[image_bytes $enabled] ne [image_bytes [file join $output_dir delgpu_graph_repeated.png]]} {
  error "Repeated graph display differs"
}
puts "pass: graph overlay enabled/disabled and repeated display"
