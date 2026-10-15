# SPDX-License-Identifier: BSD-3-Clause
# Screen and confirm feedback-based learning rates on identical initial graphs.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_adaptive_rate]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
set phase sweep
if {[info exists ::env(DELGPU_ADAPTIVE_PHASE)]} {set phase $::env(DELGPU_ADAPTIVE_PHASE)}
file mkdir $output
read_db $db
set wire_before [del::regular_wire_length_dbu_cmd]
puts "ADAPTIVE warmup"
delgpu -gui 0 -backend gpu -max_iterations 200 -learning_rate_mode linear -learning_rate 1
if {$phase eq "sweep"} {
  foreach iterations {100 125 150 200} {
    foreach pair {{1.5 0.5} {1.5 1} {2 0.5} {2 1} {3 0.5} {3 1}} {
      lassign $pair rate end
      set name [string map {. p} sweep_${iterations}_${rate}_${end}]
      puts "ADAPTIVE phase=sweep name=$name iterations=$iterations rate=$rate end=$end"
      delgpu -gui 0 -backend gpu -max_iterations $iterations -max_step 0.25 \
        -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode adaptive \
        -learning_rate $rate -learning_rate_end $end \
        -report [file join $output ${name}.csv]
    }
  }
} elseif {$phase eq "confirm"} {
  if {![info exists ::env(DELGPU_ADAPTIVE_CASES)]} {
    error "Set DELGPU_ADAPTIVE_CASES to {{name mode iterations rate end} ...}"
  }
  for {set sample 1} {$sample <= 3} {incr sample} {
    foreach case $::env(DELGPU_ADAPTIVE_CASES) {
      lassign $case name mode iterations rate end
      puts "ADAPTIVE phase=confirm name=$name mode=$mode iterations=$iterations rate=$rate end=$end sample=$sample"
      set started [clock microseconds]
      delgpu -gui 0 -backend gpu -max_iterations $iterations -max_step 0.25 \
        -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode $mode \
        -learning_rate $rate -learning_rate_end $end \
        -report [file join $output ${name}_${sample}.csv]
      puts "ADAPTIVE wall_ms=[expr {([clock microseconds]-$started)/1000.0}] name=$name sample=$sample"
    }
  }
} else {error "DELGPU_ADAPTIVE_PHASE must be sweep or confirm"}
if {[del::regular_wire_length_dbu_cmd] != $wire_before} {error "OpenDB routed wire length changed"}
puts "pass: adaptive $phase completed; routed wire length unchanged ($wire_before DBU)"
