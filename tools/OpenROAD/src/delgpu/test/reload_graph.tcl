# SPDX-License-Identifier: BSD-3-Clause
set test_dir [file dirname [file normalize [info script]]]
set ::env(DELGPU_GUI) 0
set ::env(DELGPU_VERIFY) 1
source [file join $test_dir ../scripts/open_route.tcl]
odb::dbChip_destroy [[ord::get_db] getChip]
read_db $delgpu_route_db
delgpu -max_iterations 0 -gui 0 -verify 1
puts "pass: headless database reload uses the current block and matches CPU"
