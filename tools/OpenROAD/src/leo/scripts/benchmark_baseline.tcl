# SPDX-License-Identifier: BSD-3-Clause
# Run LPA+EMA with automatic all-violation assessment and CSV inside timing.
set benchmark_script_dir [file dirname [file normalize [info script]]]
set benchmark_orfs [file normalize [file join $benchmark_script_dir ../../../../..]]
set benchmark_db [file join $benchmark_orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(LEO_ROUTE_DB)]} { set benchmark_db $::env(LEO_ROUTE_DB) }
set benchmark_output [file join $benchmark_orfs flow reports asap7 ibex base leo_baseline]
if {[info exists ::env(LEO_BENCHMARK_DIR)]} { set benchmark_output $::env(LEO_BENCHMARK_DIR) }
file mkdir $benchmark_output
read_db $benchmark_db
if {[gui::enabled]} { error "LEO timing must run without GUI" }
set wire_before [del::regular_wire_length_dbu_cmd]
puts "LEO BENCHMARK input=$benchmark_db gui=0 regular_wire_length_dbu=$wire_before"
set benchmark_prune_factor 3
if {[info exists ::env(LEO_PRUNE_FACTOR)]} { set benchmark_prune_factor $::env(LEO_PRUNE_FACTOR) }
puts "LEO BENCHMARK prune_factor=$benchmark_prune_factor P=k*Dt oracle=fresh_full_scans ema=first_improvement"
puts "LEO BENCHMARK warmup"
leo -prune_factor $benchmark_prune_factor
for {set sample 1} {$sample <= 3} {incr sample} {
  puts "LEO BENCHMARK sample=$sample"
  set started [clock microseconds]
  leo -prune_factor $benchmark_prune_factor -report [file join $benchmark_output quality_${sample}.csv]
  puts "LEO BENCHMARK sample=$sample command_ms=[expr {([clock microseconds]-$started)/1000.0}]"
}
set wire_after [del::regular_wire_length_dbu_cmd]
if {$wire_before != $wire_after} { error "LEO changed OpenDB routed wire length" }
puts "pass: paper-policy LPA+EMA completed; regular_wire_length_dbu=$wire_after unchanged"
