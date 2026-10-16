# SPDX-License-Identifier: BSD-3-Clause
# Fresh same-ODB comparison of LEO, native DEL200+LP/MIP and default DELGPU150.
set compare_script_dir [file dirname [file normalize [info script]]]
set compare_orfs [file normalize [file join $compare_script_dir ../../../../..]]
set compare_db [file join $compare_orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_COMPARE_DB)]} {set compare_db $::env(DELGPU_COMPARE_DB)}
set compare_output [file join $compare_orfs flow reports asap7 ibex base delgpu_three_methods]
if {[info exists ::env(DELGPU_COMPARE_DIR)]} {set compare_output $::env(DELGPU_COMPARE_DIR)}
file mkdir $compare_output
if {[gui::enabled]} {error "Wire extension comparison must run without GUI"}
read_db $compare_db
set compare_wire [del::regular_wire_length_dbu_cmd]
set compare_block [ord::get_db_block]
puts "COMPARE input=$compare_db dbu_per_micron=[$compare_block getDbUnitsPerMicron] original_wire_dbu=$compare_wire gui=0"
puts "COMPARE begin method=leo prune_factor=3"
set compare_start [clock microseconds]
leo -prune_factor 3 -report [file join $compare_output leo.csv]
puts "COMPARE end method=leo wall_ms=[expr {([clock microseconds]-$compare_start)/1000.0}]"
if {[del::regular_wire_length_dbu_cmd] != $compare_wire} {error "LEO modified routed wire"}
puts "COMPARE begin method=leo20 prune_factor=4 common_20W_domain_control=1"
set compare_start [clock microseconds]
leo -prune_factor 4 -report [file join $compare_output leo20.csv]
puts "COMPARE end method=leo20 wall_ms=[expr {([clock microseconds]-$compare_start)/1000.0}]"
if {[del::regular_wire_length_dbu_cmd] != $compare_wire} {error "LEO20 modified routed wire"}
puts "COMPARE begin method=delgpu iterations=150 adaptive=1.5_to_0.5 cooperative=8"
set compare_start [clock microseconds]
delgpu -gui 0 -max_iterations 150 -max_step 0.25 -jump_offset_nm 35 \
  -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5 \
  -postprocess 1 -post_sweeps 24 -post_pair_rounds 8 \
  -post_retract_mode cooperative -post_retract_sweeps 8 \
  -report [file join $compare_output delgpu.csv]
puts "COMPARE end method=delgpu wall_ms=[expr {([clock microseconds]-$compare_start)/1000.0}]"
if {[del::regular_wire_length_dbu_cmd] != $compare_wire} {error "DELGPU modified routed wire"}
puts "COMPARE begin method=del iterations=200 max_step=0.25 jump_offset_nm=35 postprocess=LP_MIP"
set compare_start [clock microseconds]
del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35 \
  -report [file join $compare_output del.csv]
c
puts "COMPARE end method=del wall_ms=[expr {([clock microseconds]-$compare_start)/1000.0}]"
if {[del::regular_wire_length_dbu_cmd] != $compare_wire} {error "DEL modified routed wire"}
puts "pass: all three methods completed on the same ODB; actual routed wire unchanged ($compare_wire DBU)"
