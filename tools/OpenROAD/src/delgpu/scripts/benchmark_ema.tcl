# SPDX-License-Identifier: BSD-3-Clause
# Compare EMA variants, reduced iteration budgets, LEO and the old MIP-style search.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_ema]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
file mkdir $output
if {[gui::enabled]} {error "EMA benchmark requires GUI disabled"}
read_db $db
set before [del::regular_wire_length_dbu_cmd]
set block [ord::get_db_block]
puts "EMA input=$db dbu_per_micron=[$block getDbUnitsPerMicron] original_wire_dbu=$before gui=0 samples=3"
puts "EMA warmup"
delgpu -gui 0 -post_retract_mode ema_lpa -post_joint_rounds 1 -post_joint_trials 64
leo
for {set sample 1} {$sample <= 3} {incr sample} {
  set cases {baseline150 ema150 jump150 seed150 seed100 seed75 mip150 leo}
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach name $cases {
    set iterations 150; set mode cooperative; set joint 0
    switch $name {
      ema150 {set mode ema}
      jump150 {set mode ema_jump}
      seed150 {set mode ema_lpa}
      seed100 {set mode ema_lpa; set iterations 100}
      seed75 {set mode ema_lpa; set iterations 75}
      mip150 {set joint 1}
    }
    puts "EMA sample=$sample name=$name iterations=$iterations mode=$mode joint=$joint"
    set start [clock microseconds]
    set report [file join $output ${name}_${sample}.csv]
    if {$name eq "leo"} {
      leo -report $report
    } else {
      delgpu -gui 0 -max_iterations $iterations -post_retract_mode $mode \
        -post_joint_rounds $joint -post_joint_trials 64 -report $report
    }
    puts "EMA wall_ms=[expr {([clock microseconds]-$start)/1000.0}] name=$name sample=$sample"
    if {[del::regular_wire_length_dbu_cmd] != $before} {error "Actual routed wire changed"}
  }
}
puts "pass: EMA comparison complete; actual routed wire unchanged ($before DBU)"
