# SPDX-License-Identifier: BSD-3-Clause
# Combine the GPU iteration wire cost with EMA and compare against LEO.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
set output [file join $orfs flow reports asap7 ibex base delgpu_ema_weight]
set weights {0.01 0.05 0.1 0.2 0.5 1 2 5 10}
set budgets {100 150}
set modes {ema_lpa}
set samples 3
foreach {key var} {DELGPU_ROUTE_DB db DELGPU_BENCHMARK_DIR output DELGPU_WIRE_WEIGHTS weights DELGPU_ITERATION_BUDGETS budgets DELGPU_RETRACT_MODES modes DELGPU_SAMPLES samples} {
  if {[info exists ::env($key)]} {set $var $::env($key)}
}
if {[gui::enabled]} {error "EMA/weight benchmark requires GUI disabled"}
file mkdir $output
read_db $db
set dbu [[ord::get_db_block] getDbUnitsPerMicron]
set before [del::regular_wire_length_dbu_cmd]
puts "EMA_WEIGHT input=$db dbu_per_micron=$dbu original_wire_dbu=$before gui=0 verify=0 samples=$samples"
set configs [list [list cooperative150 cooperative 150 0.01] [list cooperative150_w005 cooperative 150 0.05]]
foreach mode $modes {
  foreach iterations $budgets {
    foreach weight $weights {
      lappend configs [list ${mode}${iterations}_w${weight} $mode $iterations $weight]
    }
  }
}
lappend configs {leo_default leo 0 0} {leo20 leo20 0 0}
puts "EMA_WEIGHT warmup"
delgpu -gui 0 -wire_weight 0.01 -max_iterations 150 -post_retract_mode ema_lpa -post_joint_rounds 0
leo
set manifest {}
for {set sample 1} {$sample <= $samples} {incr sample} {
  set cases $configs
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach config $cases {
    lassign $config name mode iterations weight
    puts "EMA_WEIGHT begin name=$name sample=$sample mode=$mode iterations=$iterations weight=$weight"
    set report [file join $output ${name}_${sample}.csv]
    set start [clock microseconds]
    if {$mode eq "leo"} {
      leo -report $report
    } elseif {$mode eq "leo20"} {
      leo -prune_factor 4 -report $report
    } else {
      delgpu -gui 0 -verify 0 -max_iterations $iterations -wire_weight $weight \
        -max_step 0.25 -jump_offset_nm 35 -max_backtracks 0 \
        -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5 \
        -postprocess_only [expr {$iterations == 0}] -postprocess 1 -post_sweeps 24 -post_pair_rounds 8 -post_retract_sweeps 8 \
        -post_retract_mode $mode -post_joint_rounds 0 -report $report
    }
    set wall_ms [expr {([clock microseconds]-$start)/1000.0}]
    puts "EMA_WEIGHT end name=$name sample=$sample wall_ms=$wall_ms"
    if {[del::regular_wire_length_dbu_cmd] != $before} {error "Actual routed wire changed"}
    lappend manifest [list $name $sample $mode $iterations $weight $wall_ms]
  }
}
set out [open [file join $output runs.csv] w]
puts $out "name,sample,mode,iterations,weight,wall_ms"
foreach row $manifest {puts $out [join $row ,]}
close $out
puts "pass: EMA/weight comparison complete; actual routed wire unchanged ($before DBU)"
