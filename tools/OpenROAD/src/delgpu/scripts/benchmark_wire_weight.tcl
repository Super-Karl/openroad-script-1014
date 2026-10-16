# SPDX-License-Identifier: BSD-3-Clause
# Vary only the existing GPU objective's wire coefficient. No new stages.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_wire_weight]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
set weights {0.01 0.05 0.5 1.0 2.0}
if {[info exists ::env(DELGPU_WIRE_WEIGHTS)]} {set weights $::env(DELGPU_WIRE_WEIGHTS)}
set samples 3
if {[info exists ::env(DELGPU_SAMPLES)]} {set samples $::env(DELGPU_SAMPLES)}
file mkdir $output
read_db $db
set wire_before [del::regular_wire_length_dbu_cmd]
puts "WIRE_WEIGHT db=$db routed_wire_dbu=$wire_before gui=0 verify=0 iterations=150"
puts "WIRE_WEIGHT warmup"
delgpu -gui 0 -wire_weight 0.01
for {set sample 1} {$sample <= $samples} {incr sample} {
  set cases $weights
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach weight $cases {
    puts "WIRE_WEIGHT weight=$weight sample=$sample"
    set started [clock microseconds]
    delgpu -gui 0 -wire_weight $weight \
      -report [file join $output weight_${weight}_${sample}.csv]
    puts "WIRE_WEIGHT weight=$weight sample=$sample wall_ms=[expr {([clock microseconds]-$started)/1000.0}]"
    if {[del::regular_wire_length_dbu_cmd] != $wire_before} {
      error "OpenDB routed wire length changed"
    }
  }
}
puts "pass: wire weight comparison completed; routed wire length unchanged ($wire_before DBU)"
