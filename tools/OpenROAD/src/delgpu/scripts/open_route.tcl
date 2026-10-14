# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

# Load the initial routed design, then inspect the GPU and current OpenDB block.
set delgpu_script_dir [file dirname [file normalize [info script]]]
set delgpu_orfs_root [file normalize [file join $delgpu_script_dir ../../../../..]]
set delgpu_route_db [file join $delgpu_orfs_root flow results asap7 ibex base 5_2_route.odb]
if {[info exists ::env(DELGPU_ROUTE_DB)]} {
  set delgpu_route_db [file normalize $::env(DELGPU_ROUTE_DB)]
}
if {![file isfile $delgpu_route_db]} {
  error "DELGPU design database not found: $delgpu_route_db"
}
puts "DELGPU read design: $delgpu_route_db"
read_db $delgpu_route_db
delgpu
