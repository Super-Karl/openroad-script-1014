# Verify terminal DEL postprocess frames through n, c and repeated GUI runs.
# SPDX-License-Identifier: BSD-3-Clause
set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir ../../../test/helpers.tcl]
if {![gui::supported]} {puts pass; exit}
set ::env(QT_QPA_PLATFORM) offscreen
read_lef [file join $script_dir gui_repeat.lef]
read_def [file join $script_dir gui_repeat.def]
set report [make_result_file gui_postprocess.csv]
set image [make_result_file gui_postprocess.png]
set before [del::regular_wire_length_dbu_cmd]
gui::show {
  del -step -max_iterations 2 -frame_ms 0 -report $report
  n
  n
  gui::set_display_controls "DEL/M2/unrepaired_boxes" visible false
  gui::set_display_controls "DEL/M2/unrepaired_boxes" visible true
  del -step -max_iterations 2 -frame_ms 0 -report $report
  c
  del -max_iterations 2 -frame_ms 1 -report $report
  gui::save_image $image 0.4 0.9 1.1 1.15 800
} false
if {![file exists $image] || [file size $image]==0} {error "Missing final GUI frame"}
if {![file exists "[file rootname $report]_conflicts.csv"]} {error "Missing final conflicts"}
if {$before != [del::regular_wire_length_dbu_cmd]} {error "DEL changed ODB wires"}
puts pass
