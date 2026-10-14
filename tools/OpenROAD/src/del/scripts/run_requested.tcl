# SPDX-License-Identifier: BSD-3-Clause
# Execute the user's original DEL command, then advance its step-mode session.
set native_script_dir [file dirname [file normalize [info script]]]
set native_orfs_root [file normalize [file join $native_script_dir ../../../../..]]
set native_db [file join $native_orfs_root flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DEL_ROUTE_DB)]} { set native_db $::env(DEL_ROUTE_DB) }
if {![file isfile $native_db]} { error "DEL database not found: $native_db" }
if {[llength [info commands del]] == 0} { error "DEL is not registered in this OpenROAD executable" }
read_db $native_db
puts "DEL native test: database=$native_db gui=[gui::enabled]"
set native_before_wire_length [del::regular_wire_length_dbu_cmd]
set native_started [clock microseconds]
puts "DEL native test: executing del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35"
del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
set native_initialized [clock microseconds]
puts "DEL native test: executing c to complete 200 iterations and automatic postprocess"
c
set native_completed [clock microseconds]
set native_after_wire_length [del::regular_wire_length_dbu_cmd]
if {$native_after_wire_length != $native_before_wire_length} {
  error "DEL changed routed wire length"
}
puts "DEL native wall timing: setup_s=[expr {($native_initialized-$native_started)/1e6}] continue_and_postprocess_s=[expr {($native_completed-$native_initialized)/1e6}] total_s=[expr {($native_completed-$native_started)/1e6}]"
puts "pass: original DEL command completed; routed wire length unchanged ($native_after_wire_length DBU)"
