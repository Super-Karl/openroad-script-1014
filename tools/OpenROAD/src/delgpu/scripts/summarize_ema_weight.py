#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check repeatability, stage quality, and summarize EMA + wire-weight runs."""
import csv
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def read_csv(path):
    with path.open() as stream:
        return list(csv.DictReader(stream))


def summarize(root):
    log = (root / 'benchmark.log').read_text()
    if 'pass: EMA/weight comparison complete; actual routed wire unchanged' not in log:
        raise ValueError('Benchmark is incomplete')
    metadata = re.search(r'EMA_WEIGHT input=(.*?) dbu_per_micron=(\d+) original_wire_dbu=(\d+).*?samples=(\d+)', log)
    if metadata is None:
        raise ValueError('Missing benchmark metadata')
    dbu, expected_samples = int(metadata[2]), int(metadata[4])
    graph = re.search(r'DELGPU complete:.*?nodes=(\d+)', log)
    expected_bytes = int(graph[1]) * 8
    timings = {}
    active = None
    for line in log.splitlines():
        if line.startswith('EMA_WEIGHT begin '):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            active = (fields['name'], int(fields['sample']))
            timings[active] = defaultdict(float)
        elif active and line.startswith('DELGPU optimization complete:'):
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            for key in ('iteration_ms', 'postprocess_ms', 'total_ms', 'coordinate_download_bytes'):
                timings[active][key] = float(fields[key])
            if abs(float(fields['extent_um']) - float(fields['wire_extension_um'])) > 1e-9:
                raise ValueError('Absolute displacement differs from signed wire extension')
        elif active and 'LEO timing (ms):' in line:
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            timings[active]['lpa_ms'] += float(fields['LPA'])
            timings[active]['ema_ms'] += float(fields['EMA'])
        elif line.startswith('EMA_WEIGHT end '):
            active = None
    runs = defaultdict(list)
    for run in read_csv(root / 'runs.csv'):
        name, sample = run['name'], int(run['sample'])
        mode, weight, budget = run['mode'], float(run['weight']), int(run['iterations'])
        rows = read_csv(root / f'{name}_{sample}.csv')
        measured = dict(timings[name, sample])
        measured['wall_ms'] = float(run['wall_ms'])
        if mode.startswith('leo'):
            final = [r for r in rows if r['layer'] != 'TOTAL']
            quality = {
                'extension_um': round(sum(float(r['extent_um']) for r in final), 6),
                'ge5': sum(int(r['remaining_5nm']) for r in final),
                'lt5': sum(int(r['below_5nm']) for r in final),
                'layers': {r['layer']: {'extension_um': float(r['extent_um']),
                                      'ge5': int(r['remaining_5nm']), 'lt5': int(r['below_5nm'])} for r in final},
            }
        else:
            if measured.pop('coordinate_download_bytes') != expected_bytes:
                raise ValueError(f'Unexpected coordinate download count: {name}')
            if budget == 0:
                if measured['iteration_ms'] != 0 or any(r['stage'] == 'iteration' for r in rows):
                    raise ValueError(f'Postprocess-only run performed iterations: {name}')
                if any(int(r['iteration']) != 0 or int(r['candidates']) != 0 or int(r['repairs']) != 0 for r in rows):
                    raise ValueError(f'Postprocess-only run used continuous moves: {name}')
                inputs = [r for r in rows if r['stage'] in ('initial', 'continuous', 'quantized')]
                if not inputs or any(float(r['extent_dbu']) != 0 for r in inputs):
                    raise ValueError(f'Postprocess-only run did not start from zero: {name}')
            stage_rows = {stage: [r for r in rows if r['stage'] == stage]
                          for stage in ('continuous', 'quantized', 'repaired', 'retracted', 'final')}
            if not all(stage_rows.values()):
                raise ValueError(f'Missing stage: {name}')
            if any(float(r['wire_weight']) != weight for r in rows):
                raise ValueError(f'Incorrect wire weight: {name}')
            if any(int(r['conflicts']) != int(r['remaining_5nm']) + int(r['violations_lt5nm']) for r in rows):
                raise ValueError(f'Incorrect violation classification: {name}')
            final = stage_rows['final']
            if len({r['layer'] for r in final}) != len(final):
                raise ValueError(f'Duplicate final layer: {name}')
            repaired = {r['layer']: r for r in stage_rows['repaired']}
            for r in final:
                for key in ('conflicts', 'remaining_5nm', 'extent_dbu', 'max_gap_dbu', 'sum_gap_dbu'):
                    if float(r[key]) > float(repaired[r['layer']][key]) + 1e-8:
                        raise ValueError(f'Postprocess worsened {key}: {name}/{r["layer"]}')
            quality = {'extension_um': sum(float(r['extent_dbu']) for r in final) / dbu,
                       'ge5': sum(int(r['remaining_5nm']) for r in final),
                       'lt5': sum(int(r['violations_lt5nm']) for r in final),
                       'layers': {r['layer']: {'extension_um': float(r['extent_dbu']) / dbu,
                                             'ge5': int(r['remaining_5nm']), 'lt5': int(r['violations_lt5nm'])} for r in final}}
            for stage, values in stage_rows.items():
                quality[f'{stage}_extension_um'] = sum(float(r['extent_dbu']) for r in values) / dbu
                quality[f'{stage}_ge5'] = sum(int(r['remaining_5nm']) for r in values)
                quality[f'{stage}_lt5'] = sum(int(r['violations_lt5nm']) for r in values)
            measured['retract_ms'] = sum(float(r['stage_ms']) for r in stage_rows['retracted'])
        if not final:
            raise ValueError(f'No final rows: {name}')
        runs[name].append(dict(sample=sample, mode=mode, weight=weight, iterations=budget,
                               quality=quality, timing=measured))
    summary = {}
    for name, values in runs.items():
        if sorted(v['sample'] for v in values) != list(range(1, expected_samples + 1)):
            raise ValueError(f'Incorrect sample count: {name}')
        if any(v['quality'] != values[0]['quality'] for v in values):
            raise ValueError(f'Nonrepeatable quality: {name}')
        summary[name] = dict(mode=values[0]['mode'], weight=values[0]['weight'], iterations=values[0]['iterations'],
                             samples=len(values), **values[0]['quality'],
                             **{key: statistics.median(v['timing'][key] for v in values) for key in values[0]['timing']})
    result = dict(input=metadata[1], dbu_per_micron=dbu, original_wire_dbu=int(metadata[3]),
                  actual_odb_wire_unchanged=True, methods=summary, runs=dict(runs))
    (root / 'summary.json').write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n')
    keys = ('mode', 'weight', 'iterations', 'samples', 'extension_um', 'ge5', 'lt5',
            'continuous_extension_um', 'continuous_ge5', 'continuous_lt5',
            'iteration_ms', 'postprocess_ms', 'retract_ms', 'total_ms', 'wall_ms', 'lpa_ms', 'ema_ms')
    with (root / 'summary.csv').open('w', newline='') as out:
        writer = csv.writer(out)
        writer.writerow(['name', *keys])
        writer.writerows([name, *(v.get(k, '') for k in keys)] for name, v in summary.items())
    lines = ['| 配置 | 增加量 µm | ≥5nm | 0<残差<5nm | 迭代 ms | 后处理 ms | 命令 ms |',
             '| --- | ---: | ---: | ---: | ---: | ---: | ---: |']
    for name, v in summary.items():
        lines.append(f'| {name} | {v["extension_um"]:.3f} | {v["ge5"]} | {v["lt5"]} | '
                     f'{v.get("iteration_ms", 0):.3f} | {v.get("postprocess_ms", 0):.3f} | {v["wall_ms"]:.3f} |')
    (root / 'summary.md').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    print('pass: repeated quality, postprocess monotonicity, GUI-off final downloads and dbWire preservation checked')


if __name__ == '__main__':
    summarize(Path(sys.argv[1]))
