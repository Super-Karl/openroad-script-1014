# Shared GUI branches must survive repeated DEL/LEO runs and database reloads.
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir ../../../test/helpers.tcl]

if { ![gui::supported] } {
  puts "pass"
  exit
}
if { ![info exists ::env(QT_QPA_PLATFORM)] } {
  set ::env(QT_QPA_PLATFORM) offscreen
}

read_lef [file join $script_dir gui_repeat.lef]
read_def [file join $script_dir gui_repeat.def]
set checkpoint [make_result_file gui_repeat.odb]
set image [make_result_file gui_repeat.png]
write_db $checkpoint

proc check_del_controls {} {
  gui::set_display_controls "DEL/M2/unrepaired_boxes" visible false
  gui::set_display_controls "DEL/M2/unrepaired_boxes" visible true
  foreach group {DEL LEO} {
    foreach leaf {trims vnodes arcs/A arcs/B arcs/C} {
      gui::set_display_controls "$group/M2/$leaf" visible false
      gui::set_display_controls "$group/M2/$leaf" visible true
    }
  }
}

gui::show {
  leo
  del -step -max_iterations 4 -frame_ms 1
  n
  c
  check_del_controls
  for {set i 0} {$i < 3} {incr i} {
    del -max_iterations 4 -frame_ms 1
    leo
    check_del_controls
  }
  odb::dbChip_destroy [[ord::get_db] getChip]
  read_db $checkpoint
  del -max_iterations 4 -frame_ms 1
  leo
  check_del_controls
  gui::save_image $image 0.4 0.9 1.1 1.15 800
} false

if { [file exists $image] && [file size $image] > 0 } {
  puts "pass"
} else {
  error "DEL GUI image was not created"
}
