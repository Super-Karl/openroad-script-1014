# SPDX-License-Identifier: BSD-3-Clause
# Same-ODB GPU interval/network/bounded-search comparison, GUI disabled.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_joint]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
file mkdir $output
if {[gui::enabled]} {error "Joint benchmark requires GUI disabled"}
read_db $db
set before [del::regular_wire_length_dbu_cmd]
set block [ord::get_db_block]
puts "JOINT input=$db dbu_per_micron=[$block getDbUnitsPerMicron] original_wire_dbu=$before gui=0 samples=3"
puts "JOINT warmup"
delgpu -gui 0 -post_joint_rounds 1 -post_joint_trials 64
for {set sample 1} {$sample <= 3} {incr sample} {
  set cases {baseline network balanced quality}
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach name $cases {
    switch $name {
      baseline {set rounds 0; set trials 16}
      network {set rounds 1; set trials 2}
      balanced {set rounds 1; set trials 16}
      quality {set rounds 1; set trials 64}
    }
    puts "JOINT sample=$sample name=$name rounds=$rounds trials=$trials"
    set start [clock microseconds]
    delgpu -gui 0 -post_joint_rounds $rounds -post_joint_trials $trials \
      -report [file join $output ${name}_${sample}.csv]
    puts "JOINT wall_ms=[expr {([clock microseconds]-$start)/1000.0}] name=$name sample=$sample"
    if {[del::regular_wire_length_dbu_cmd] != $before} {error "Actual routed wire changed"}
  }
}
puts "pass: joint comparison complete; actual routed wire unchanged ($before DBU)"
