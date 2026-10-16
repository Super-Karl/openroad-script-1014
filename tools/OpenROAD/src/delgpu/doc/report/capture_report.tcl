# Capture actual default 150-round GUI results for the saved report.
set root [file normalize [file join [file dirname [info script]] ../../../../../..]]
set out [file join [file dirname [file normalize [info script]]] assets]
file mkdir $out
read_db [file join $root flow results asap7 ibex base 5_2_route.odb]
proc controls {domains} {
  gui::set_display_controls "Layers/*" visible false
  gui::set_display_controls "Instances/*" visible false
  gui::set_display_controls "DELGPU/*" visible false
  gui::set_display_controls "DELGPU/M2/*" visible true
  gui::set_display_controls "DELGPU/M2/domains" visible $domains
}
proc save_views {out prefix} {
  gui::save_image [file join $out ${prefix}_context.png] 20 20 24 24 1400
  gui::save_image [file join $out ${prefix}_detail.png] 20.9 20.9 21.6 21.6 1400
}
delgpu -gui 1 -max_iterations 0
controls false
save_views $out gui_initial
controls true
gui::save_image [file join $out gui_domains_detail.png] 20.9 20.9 21.6 21.6 1400
# Keep the algorithm identical, disable only postprocess for the continuous view.
delgpu -gui 1 -frame_ms 0 -postprocess 0 -report [file join $out ../data/gui_continuous.csv]
controls false
save_views $out gui_continuous
delgpu -gui 1 -frame_ms 0 -report [file join $out ../data/gui_final.csv]
controls false
save_views $out gui_final
delgpu -gui 0 -max_iterations 0
gui::save_image [file join $out gui_disabled_detail.png] 20.9 20.9 21.6 21.6 1400
puts "pass: report captures graph, domains, continuous150, full150 and disabled GUI"
