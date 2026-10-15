# SPDX-License-Identifier: BSD-3-Clause
# Compare native CPU semantics and GPU retry budgets at continuous iteration 200.
set benchmark_script_dir [file dirname [file normalize [info script]]]
set benchmark_orfs [file normalize [file join $benchmark_script_dir ../../../../..]]
set benchmark_db [file join $benchmark_orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} { set benchmark_db $::env(DELGPU_ROUTE_DB) }
set benchmark_output [file join $benchmark_orfs flow reports asap7 ibex base delgpu_cpu_jump_200]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} { set benchmark_output $::env(DELGPU_BENCHMARK_DIR) }
file mkdir $benchmark_output
read_db $benchmark_db
set benchmark_wire_before [del::regular_wire_length_dbu_cmd]
puts "BENCHMARK input=$benchmark_db regular_wire_length_dbu=$benchmark_wire_before"
puts "BENCHMARK warmup: GPU CPU-compatible jumps, 19 retries"
delgpu -gui 0 -backend gpu -max_iterations 200 -max_step 0.25 \
  -jump_offset_nm 35 -max_backtracks 19 -learning_rate_mode linear -learning_rate 1
for {set sample 1} {$sample <= 3} {incr sample} {
  foreach method {cpu gpu0 gpu7 gpu19} {
    set options [list -gui 0 -max_iterations 200 -max_step 0.25 -jump_offset_nm 35 \
      -report [file join $benchmark_output ${method}_${sample}.csv]]
    if {$method eq "cpu"} {
      lappend options -backend cpu
    } else {
      lappend options -backend gpu -learning_rate_mode linear -learning_rate 1 -max_backtracks [string range $method 3 end]
    }
    puts "BENCHMARK sample=$sample method=$method"
    set started [clock microseconds]
    delgpu {*}$options
    puts "BENCHMARK wall_ms=[expr {([clock microseconds]-$started)/1000.0}] sample=$sample method=$method"
  }
}
set benchmark_wire_after [del::regular_wire_length_dbu_cmd]
if {$benchmark_wire_before != $benchmark_wire_after} { error "Trim optimization changed OpenDB routed wire length" }
puts "pass: CPU/GPU continuous-stage benchmark completed; regular_wire_length_dbu=$benchmark_wire_after unchanged"
