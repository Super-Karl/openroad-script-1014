# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

# Load the detailed-route checkpoint selected by leo.sh and keep the GUI open.
read_db $::env(LEO_ROUTE_DB)
gui::set_title "LEO - ASAP7 Ibex - 5_2_route"

# Run the LEO framework automatically after the routed database is loaded.
leo
