# SPDX-License-Identifier: BSD-3-Clause
# Compare retraction methods on the identical continuous and repaired states.
set script_dir [file dirname [file normalize [info script]]]
set orfs [file normalize [file join $script_dir ../../../../..]]
set db [file join $orfs flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {set db $::env(DELGPU_ROUTE_DB)}
set output [file join $orfs flow reports asap7 ibex base delgpu_retraction]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {set output $::env(DELGPU_BENCHMARK_DIR)}
file mkdir $output
read_db $db
set wire_before [del::regular_wire_length_dbu_cmd]
puts "RETRACTION warmup"
delgpu -gui 0
for {set sample 1} {$sample <= 3} {incr sample} {
  set cases {{single single 8} {cooperative cooperative 8} {cooperative16 cooperative 16} {cooperative32 cooperative 32}}
  if {$sample % 2 == 0} {set cases [lreverse $cases]}
  foreach case $cases {
    lassign $case name mode sweeps
    puts "RETRACTION sample=$sample name=$name mode=$mode sweeps=$sweeps"
    set started [clock microseconds]
    delgpu -gui 0 -post_retract_mode $mode -post_retract_sweeps $sweeps \
      -report [file join $output ${name}_${sample}.csv]
    puts "RETRACTION wall_ms=[expr {([clock microseconds]-$started)/1000.0}] name=$name sample=$sample"
  }
}
if {[del::regular_wire_length_dbu_cmd] != $wire_before} {error "OpenDB routed wire length changed"}
puts "pass: retraction comparison completed; routed wire length unchanged ($wire_before DBU)"
