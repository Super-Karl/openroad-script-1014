# SPDX-License-Identifier: BSD-3-Clause
# Compare the same deterministic 150-round GPU result with each postprocess stage.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_postprocess]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
file mkdir $output
read_db $db
set wire_before [del::regular_wire_length_dbu_cmd]
puts "POSTPROCESS warmup"
delgpu -gui 0
for {set sample 1} {$sample <= 3} {incr sample} {
  set cases {continuous quantized repaired full}
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach name $cases {
    set options [list -postprocess 1]
    switch $name {
      continuous {set options [list -postprocess 0]}
      quantized {lappend options -post_sweeps 0 -post_pair_rounds 0 -post_retract_sweeps 0}
      repaired {lappend options -post_retract_sweeps 0}
    }
    puts "POSTPROCESS sample=$sample name=$name"
    set started [clock microseconds]
    delgpu -gui 0 {*}$options -report [file join $output ${name}_${sample}.csv]
    puts "POSTPROCESS wall_ms=[expr {([clock microseconds]-$started)/1000.0}] name=$name sample=$sample"
  }
}
if {[del::regular_wire_length_dbu_cmd] != $wire_before} {error "OpenDB routed wire length changed"}
puts "pass: postprocess comparison completed; routed wire length unchanged ($wire_before DBU)"
