#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Summarize a fresh same-ODB length comparison, including final postprocess."""
import csv
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
log = (root / 'run.log').read_text()
assert 'pass: all three methods completed on the same ODB' in log
meta = re.search(r'COMPARE input=(.*?) dbu_per_micron=(\d+) original_wire_dbu=(\d+) gui=0', log)
assert meta
units, original = int(meta[2]), int(meta[3])
original_um = original / units

def load(name):
    with (root / name).open() as f:
        return list(csv.DictReader(f))

leo_rows = [r for r in load('leo.csv') if r['layer'] != 'TOTAL']
gpu_rows = [r for r in load('delgpu.csv') if r['stage'] == 'final']
del_rows = load('del_postprocess.csv')
conflicts = load('del_conflicts.csv')
assert len(leo_rows) == len(gpu_rows) == len(del_rows) == 6
methods = {}
methods['LEO'] = {'configuration': 'prune_factor=3, LPA+EMA', 'layers': {
    r['layer']: {'extension_um': float(r['extent_um']), 'c5': int(r['remaining_5nm']),
                 'small': int(r['below_5nm']), 'conflicts': int(r['violations'])}
    for r in leo_rows}}
methods['DEL'] = {'configuration': '200 iterations, max_step=0.25, jump=35nm, LP/MIP', 'layers': {
    r['layer']: {'extension_um': float(r['final_extent_um']),
                 'c5': int(r['exact_conflicts']) - int(r['below_5nm']),
                 'small': int(r['below_5nm']), 'conflicts': int(r['exact_conflicts'])}
    for r in del_rows}}
methods['DELGPU'] = {'configuration': 'adaptive150 1.5->0.5, integer repair, cooperative8', 'layers': {
    r['layer']: {'extension_um': float(r['extent_dbu']) / units,
                 'c5': int(r['remaining_5nm']), 'small': int(r['violations_lt5nm']),
                 'conflicts': int(r['conflicts'])}
    for r in gpu_rows}}
for method, values in methods.items():
    values['extension_um'] = sum(r['extension_um'] for r in values['layers'].values())
    values['increase_percent'] = values['extension_um'] / original_um * 100
    for key in ('c5', 'small', 'conflicts'):
        values[key] = sum(r[key] for r in values['layers'].values())
    assert values['conflicts'] == values['c5'] + values['small']
    for r in values['layers'].values():
        assert r['conflicts'] == r['c5'] + r['small']
    measured = re.search(rf'COMPARE end method={method.lower()} wall_ms=([\d.]+)', log)
    assert measured
    values['single_run_command_ms'] = float(measured[1])
assert len(conflicts) == methods['DEL']['conflicts']
assert sum(float(r['repair_distance_nm']) >= 5 for r in conflicts) == methods['DEL']['c5']
assert all(abs(float(r['extent_um']) - float(r['wire_extension_um'])) < 1e-9 for r in leo_rows)
for values in methods.values():
    values['extension_um'] = round(values['extension_um'], 6)
comparison = {'input': meta[1], 'original_wire_dbu': original, 'dbu_per_micron': units,
              'original_wire_um': original_um, 'measure': 'sum(abs(trim displacement)), not written-back wire',
              'actual_odb_wire_unchanged': True, 'methods': methods,
              'note': 'One fresh quality run each; command timings are not a repeated speed benchmark. LEO uses pruned 15W domains; DEL and DELGPU use capped 20W domains.'}
gpu = methods['DELGPU']
comparison['gpu_extra'] = {}
for baseline in ('LEO', 'DEL'):
    difference = gpu['extension_um'] - methods[baseline]['extension_um']
    comparison['gpu_extra'][baseline] = {'um': round(difference, 6),
        'percent_of_baseline_extension': difference / methods[baseline]['extension_um'] * 100}
if (root / 'leo20.csv').is_file():
    control = next(r for r in load('leo20.csv') if r['layer'] == 'TOTAL')
    comparison['leo20_control'] = {
        'configuration': 'prune_factor=4, P=20W',
        'nodes': int(control['nodes']), 'arcs': int(control['arcs']),
        'extension_um': float(control['extent_um']),
        'c5': int(control['remaining_5nm']), 'small': int(control['below_5nm']),
        'conflicts': int(control['violations'])}
(root / 'comparison.json').write_text(json.dumps(comparison, ensure_ascii=False, indent=2) + '\n')
with (root / 'comparison.csv').open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['method', 'added_length_um', 'increase_percent', 'remaining_5nm', 'violations_lt5nm', 'all_violations'])
    for name, v in methods.items():
        writer.writerow([name, v['extension_um'], v['increase_percent'], v['c5'], v['small'], v['conflicts']])
with (root / 'layers.csv').open('w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['layer','leo_um','del_um','delgpu_um','gpu_minus_leo_um','gpu_minus_del_um'])
    for layer in gpu['layers']:
        lengths = [methods[m]['layers'][layer]['extension_um'] for m in methods]
        writer.writerow([layer, *lengths, round(lengths[2]-lengths[0],6), round(lengths[2]-lengths[1],6)])
lines = ['| 方法 | 增加线长 µm | 相对原线长增幅 | ≥5nm | 0<残差<5nm | 全部违例 |',
         '| --- | ---: | ---: | ---: | ---: | ---: |']
for name,v in methods.items():
    lines.append(f"| {name} | {v['extension_um']:.3f} | {v['increase_percent']:.4f}% | {v['c5']} | {v['small']} | {v['conflicts']} |")
(root / 'summary.md').write_text('\n'.join(lines)+'\n')
print('\n'.join(lines))
print(json.dumps(comparison['gpu_extra'],ensure_ascii=False,indent=2))
print('pass: all final-stage counts and extension sums validated; original dbWire unchanged')
