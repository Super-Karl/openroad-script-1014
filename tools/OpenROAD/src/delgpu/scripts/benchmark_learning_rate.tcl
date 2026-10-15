# SPDX-License-Identifier: BSD-3-Clause
# Sweep learning rate at fixed CPU-compatible jumps, then confirm shorter runs.
set lr_script_dir [file dirname [file normalize [info script]]]
set lr_orfs [file normalize [file join $lr_script_dir ../../../../..]]
set lr_db [file join $lr_orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} { set lr_db $::env(DELGPU_ROUTE_DB) }
set lr_output [file join $lr_orfs flow reports asap7 ibex base delgpu_learning_rate]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} { set lr_output $::env(DELGPU_BENCHMARK_DIR) }
set lr_phase sweep
if {[info exists ::env(DELGPU_LR_PHASE)]} { set lr_phase $::env(DELGPU_LR_PHASE) }
file mkdir $lr_output
read_db $lr_db
set lr_wire_before [del::regular_wire_length_dbu_cmd]
puts "LRBENCH warmup rate=1 iterations=200"
delgpu -gui 0 -backend gpu -max_iterations 200 -max_step 0.25 \
  -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode linear -learning_rate 1
if {$lr_phase eq "sweep"} {
  foreach rate {0.5 0.75 1 1.25 1.5 2 3 4} {
    set tag [string map {. p} $rate]
    puts "LRBENCH phase=sweep rate=$rate iterations=200"
    delgpu -gui 0 -backend gpu -max_iterations 200 -max_step 0.25 \
      -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode linear -learning_rate $rate \
      -report [file join $lr_output sweep_lr_${tag}.csv]
  }
} elseif {$lr_phase eq "anneal"} {
  foreach iterations {100 125 150} {
    foreach pair {{1.25 1} {1.5 1} {2 1} {2 0.75} {2 0.5} {3 0.5}} {
      lassign $pair rate end_rate
      set tag [string map {. p} ${rate}_${end_rate}_${iterations}]
      puts "LRBENCH phase=anneal rate=$rate end_rate=$end_rate iterations=$iterations"
      delgpu -gui 0 -backend gpu -max_iterations $iterations -max_step 0.25 \
        -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode linear -learning_rate $rate \
        -learning_rate_end $end_rate -report [file join $lr_output anneal_${tag}.csv]
    }
  }
} elseif {$lr_phase eq "confirm"} {
  if {![info exists ::env(DELGPU_LR_CASES)]} {
    error "Set DELGPU_LR_CASES to Tcl lists: {{name rate iterations ?end_rate?} ...}"
  }
  for {set sample 1} {$sample <= 3} {incr sample} {
    foreach case $::env(DELGPU_LR_CASES) {
      lassign $case name rate iterations end_rate
      if {$end_rate eq ""} { set end_rate $rate }
      puts "LRBENCH phase=confirm name=$name rate=$rate end_rate=$end_rate iterations=$iterations sample=$sample"
      set started [clock microseconds]
      delgpu -gui 0 -backend gpu -max_iterations $iterations -max_step 0.25 \
        -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode linear -learning_rate $rate \
        -learning_rate_end $end_rate \
        -report [file join $lr_output ${name}_${sample}.csv]
      puts "LRBENCH wall_ms=[expr {([clock microseconds]-$started)/1000.0}] name=$name sample=$sample"
    }
  }
} else {
  error "DELGPU_LR_PHASE must be sweep, anneal or confirm"
}
if {[del::regular_wire_length_dbu_cmd] != $lr_wire_before} {
  error "Learning-rate benchmark changed OpenDB routed wire length"
}
puts "pass: learning-rate $lr_phase completed; routed wire length unchanged ($lr_wire_before DBU)"
