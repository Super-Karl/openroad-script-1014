# Verify the 100-iteration cap, CSV reporting, and read-only repeated runs.
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
set script_dir [file dirname [file normalize [info script]]]
source [file join $script_dir ../../../test/helpers.tcl]
foreach args {{-max_step 0} {-jump_offset_nm 0} {-max_iterations -1} {-max_iterations 0} {-frame_ms -1} {-max_iterations abc}} {
  if { ![catch {del {*}$args}] } { error "DEL accepted invalid options: $args" }
}
if { ![catch {c}] } { error "c accepted a missing session" }
if { ![catch {c unexpected}] } { error "c accepted an argument" }
# The former 100-round cap is removed; preserve configuration across calls.
del_configure -max_iterations 201 -max_step 0.3 -jump_offset_nm 8
set configured [del_configure]
if {[dict get $configured max_iterations] != 201 || [dict get $configured max_step] != 0.3 || [dict get $configured jump_offset_nm] != 8} {
  error "DEL global parameters were not retained"
}
del_configure -max_iterations 100 -max_step 0.2 -jump_offset_nm 5
read_lef [file join $script_dir gui_repeat.lef]
read_def [file join $script_dir gui_repeat.def]
set checkpoint [make_result_file optimizer_continue_reload.odb]
write_db $checkpoint
set before [make_result_file optimizer_before.def]
set after [make_result_file optimizer_after.def]
set report [make_result_file optimizer_iterations.csv]
write_def $before

del -max_iterations 100 -frame_ms 0 -report $report
set f [open $report r]
set rows [split [string trim [read $f]] \n]
close $f
if { [llength $rows] != 203 } { error "Expected header and 101 M2/TOTAL frames" }
set initial [split [lindex $rows 2] ,]
set final [split [lindex $rows end] ,]
if { [lindex $final 0] != 100 || [lindex $final 1] ne "TOTAL" } { error "Incorrect iteration budget" }
if { [lindex $final 4] >= [lindex $initial 4] } { error "Rule energy did not decrease" }
set stepped [make_result_file optimizer_continue.csv]
del -step -max_iterations 100 -frame_ms 0 -report $stepped
n
n
# Change future defaults without changing the active session snapshot.
del_configure -max_iterations 201 -max_step 0.1 -jump_offset_nm 9
c
set f [open $stepped r]
set continued_rows [split [string trim [read $f]] \n]
close $f
if { [llength $continued_rows] != [llength $rows] } { error "c repeated or skipped iterations" }
foreach automatic [lrange $rows 1 end] continued [lrange $continued_rows 1 end] {
  # Exclude only cumulative elapsed_s (column 12).
  set a [lreplace [split $automatic ,] 12 12]
  set b [lreplace [split $continued ,] 12 12]
  if { $a ne $b } { error "n/n/c does not match automatic optimization" }
}
set finished_size [file size $stepped]
foreach suffix {_postprocess.csv _postprocess_displacements.csv _conflicts.csv} {
  set f [open "[file rootname $report]$suffix" r]; set a [read $f]; close $f
  set f [open "[file rootname $stepped]$suffix" r]; set b [read $f]; close $f
  if {$a ne $b} { error "Postprocess differs between del and n/n/c: $suffix" }
}
set f [open "[file rootname $report]_postprocess.csv" r]
set summary [split [string trim [read $f]] \n]; close $f
set count 0
foreach row [lrange $summary 1 end] { incr count [lindex [split $row ,] 4] }
set f [open "[file rootname $report]_conflicts.csv" r]
set conflicts [split [string trim [read $f]] \n]; close $f
if {[llength $conflicts] - 1 != $count} { error "Final conflict count differs from CSV" }
set final_positions "[file rootname $report]_postprocess_displacements.csv"
set final_size [file size $final_positions]
c
n
if {[file size $final_positions] != $final_size} { error "Completed postprocess repeated" }
if { [file size $stepped] != $finished_size } { error "Completed session advanced again" }
write_def $after
set f [open $before r]; set original [read $f]; close $f
set f [open $after r]; set result [read $f]; close $f
if { $original ne $result } { error "DEL modified the routing database" }
del -graph_only -frame_ms 0
c
# A database reload must invalidate the retained session before either command.
odb::dbChip_destroy [[ord::get_db] getChip]
read_db $checkpoint
if { ![catch {c}] } { error "c accepted a released database" }
puts "pass"
