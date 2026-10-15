# SPDX-License-Identifier: BSD-3-Clause
# Test application defaults: 150 iterations, 0.25W step, 35nm jump, adaptive rate 1.5 to 0.5 (GPU).
set benchmark_script_dir [file dirname [file normalize [info script]]]
set ::env(DELGPU_GUI) 0
set ::env(DELGPU_VERIFY) 0
set ::env(DELGPU_ITERATIONS) 0
set ::env(DELGPU_BACKEND) gpu
source [file join $benchmark_script_dir open_route.tcl]
set benchmark_output [file normalize [file join $delgpu_orfs_root flow reports asap7 ibex base delgpu_defaults_150_adaptive_15_05]]
if {[info exists ::env(DELGPU_BENCHMARK_DIR)]} {
  set benchmark_output [file normalize $::env(DELGPU_BENCHMARK_DIR)]
}
file mkdir $benchmark_output
puts "DELGPU benchmark: GPU warmup; GUI off, using application defaults"
delgpu -gui 0 -backend gpu
for {set sample 1} {$sample <= 3} {incr sample} {
  foreach backend {cpu gpu} {
    puts "DELGPU benchmark: sample=$sample backend=$backend"
    delgpu -gui 0 -backend $backend \
      -report [file join $benchmark_output ${backend}_${sample}.csv]
  }
}
puts "pass: CPU/GPU defaults benchmark completed; CSV directory=$benchmark_output"
