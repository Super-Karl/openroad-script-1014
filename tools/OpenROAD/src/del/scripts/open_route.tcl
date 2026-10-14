# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

# Load the detailed-route checkpoint selected by del.sh and keep the GUI open.
read_db $::env(DEL_ROUTE_DB)
gui::set_title "DEL - ASAP7 Ibex - 5_2_route"

# Build the initial graph; n advances one iteration, c runs to the limit.
set options {}
set report [file normalize del_iterations.csv]
foreach {env_name option} {DEL_MAX_ITERATIONS -max_iterations DEL_MAX_STEP -max_step DEL_JUMP_OFFSET_NM -jump_offset_nm} {
  if {[info exists ::env($env_name)]} { lappend options $option $::env($env_name) }
}
if { [info exists ::env(DEL_REPORT)] } { set report $::env(DEL_REPORT) }
del -step {*}$options -report $report
