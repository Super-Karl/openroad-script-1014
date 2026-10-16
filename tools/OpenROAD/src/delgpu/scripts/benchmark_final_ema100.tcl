# SPDX-License-Identifier: BSD-3-Clause
# Verify final application defaults against explicit EMA100 settings.
set script_dir [file dirname [file normalize [info script]]]
set root [file normalize [file join $script_dir ../../../../..]]
set db [file join $root flow results asap7 ibex base 5_2_route.odb]
set output [file join $root flow reports asap7 ibex base delgpu_final_ema100]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
if {[gui::enabled]} {error "Final-default timing requires GUI disabled"}
file mkdir $output
read_db $db
set before [del::regular_wire_length_dbu_cmd]
puts "FINAL_EMA100 input=$db dbu_per_um=[[ord::get_db_block] getDbUnitsPerMicron] wire_before_dbu=$before"
proc run_final {mode report} {
  if {$mode eq "default"} {
    delgpu -report $report
  } else {
    delgpu -gui 0 -verify 0 -backend gpu -max_iterations 100 -max_step 0.25 \
      -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode adaptive \
      -learning_rate 1.5 -learning_rate_end 0.5 -wire_weight 0.1 \
      -postprocess 1 -postprocess_only 0 -post_sweeps 24 -post_pair_rounds 8 \
      -post_retract_sweeps 8 -post_retract_mode ema_lpa \
      -post_joint_rounds 0 -post_joint_trials 16 -frame_ms 100 -report $report
  }
}
run_final default [file join $output warmup.csv]
for {set sample 1} {$sample <= 3} {incr sample} {
  set cases {default explicit}
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach mode $cases {
    set start [clock microseconds]
    run_final $mode [file join $output ${mode}_${sample}.csv]
    puts "FINAL_EMA100 mode=$mode sample=$sample command_ms=[expr {([clock microseconds]-$start)/1000.0}]"
  }
}
if {[del::regular_wire_length_dbu_cmd] != $before} {error "Actual routed wire length changed"}
if {![catch {delgpu -integer_iterations 1} message]} {error "Removed integer option still accepted"}
puts "pass: integer-iteration option removed: $message"
puts "pass: final EMA100 defaults benchmark; routed wire length unchanged"
