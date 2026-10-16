#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Independent MILP diagnosis; never changes the LEO solver or the ODB.

Requires numpy/scipy. All coordinates are integer doubled DBU. Previously
legal arcs remain mandatory, but their legal interval may change. First
maximize satisfied arcs, then minimize extension at that count.
"""
import argparse
import json
import time

import numpy as np
from scipy.optimize import Bounds, LinearConstraint, milp
from scipy.sparse import coo_matrix, vstack


def arc_gap(arc, x):
    delta = x[arc['dst']] - x[arc['src']]
    return min(max(0, lo - delta if lo is not None else 0,
                   delta - hi if hi is not None else 0)
               for lo, hi in arc['ranges'])


def solve_case(case):
    nodes, arcs = case['nodes'], case['arcs']
    n = len(nodes)
    idx = {node['id']: i for i, node in enumerate(nodes)}
    baseline = {node['id']: node['position'] for node in nodes}
    baseline_gaps = [arc_gap(arc, baseline) for arc in arcs]
    baseline_bad = sum(g > 0 for g in baseline_gaps)
    baseline_length = sum(node['weight'] * (node['position'] - node['initial'])
                          for node in nodes) / 2 / case['dbu']
    choices = [(a, lo, hi) for a, arc in enumerate(arcs)
               for lo, hi in arc['ranges']]
    m = n + len(choices)
    origin = min(node['low'] for node in nodes)
    lower = np.array([node['low'] - origin for node in nodes] + [0] * len(choices), dtype=float)
    upper = np.array([node['up'] - origin for node in nodes] + [1] * len(choices), dtype=float)
    rows, lb, ub = [], [], []
    groups = [[] for arc in arcs]
    for q, (a, lo, hi) in enumerate(choices, n):
        arc = arcs[a]
        i, j = idx[arc['src']], idx[arc['dst']]
        groups[a].append(q)
        if lo is not None:
            big_m = max(0, lo - (lower[j] - upper[i]))
            rows.append({j: 1, i: -1, q: -big_m})
            lb.append(lo - big_m)
            ub.append(np.inf)
        if hi is not None:
            big_m = max(0, upper[j] - lower[i] - hi)
            rows.append({j: 1, i: -1, q: big_m})
            lb.append(-np.inf)
            ub.append(hi + big_m)
    for a, group in enumerate(groups):
        rows.append({q: 1 for q in group})
        # Protect the UNION of all legal ranges, not LPA's single chosen range.
        lb.append(1 if baseline_gaps[a] == 0 else 0)
        ub.append(1)
    rr, cc, vv = [], [], []
    for r, row in enumerate(rows):
        for c, v in row.items():
            rr.append(r)
            cc.append(c)
            vv.append(v)
    matrix = coo_matrix((vv, (rr, cc)), shape=(len(rows), m)).tocsc()
    objective = np.r_[np.zeros(n), -np.ones(len(choices))]
    limits = Bounds(lower, upper)
    integer = np.ones(m)
    options = {'time_limit': 2.0, 'mip_rel_gap': 0.0}
    first = milp(objective, integrality=integer, bounds=limits,
                 constraints=LinearConstraint(matrix, lb, ub), options=options)
    record = {'layer': case['layer'], 'component': case['component'],
              'nodes': n, 'arcs': len(arcs), 'baseline_violations': baseline_bad,
              'baseline_extension_um': baseline_length, 'count_status': int(first.status)}
    if first.status != 0:
        record['message'] = first.message
        return record
    kept = int(round(-first.fun))
    record['minimum_violations_protecting_legal_arcs'] = len(arcs) - kept
    count_row = coo_matrix((np.ones(len(choices)),
                            (np.zeros(len(choices), dtype=int), np.arange(n, m))),
                           shape=(1, m)).tocsc()
    all_matrix = vstack([matrix, count_row], format='csc')
    cost = np.r_[[node['weight'] for node in nodes], np.zeros(len(choices))]
    second = milp(cost, integrality=integer, bounds=limits,
                  constraints=LinearConstraint(all_matrix, lb + [kept], ub + [kept]),
                  options=options)
    record['length_status'] = int(second.status)
    candidate = second if second.status == 0 else first
    rounded = np.rint(candidate.x)
    if np.max(np.abs(candidate.x - rounded)) > 1e-5:
        raise RuntimeError('noninteger candidate')
    if np.any(rounded < lower) or np.any(rounded > upper):
        raise RuntimeError('candidate outside hard domains')
    positions = {node['id']: int(rounded[i]) + origin for i, node in enumerate(nodes)}
    new_gaps = [arc_gap(arc, positions) for arc in arcs]
    for old, new in zip(baseline_gaps, new_gaps):
        if old == 0 and new != 0:
            raise RuntimeError('a previously legal arc became illegal')
    violations = sum(g > 0 for g in new_gaps)
    if violations != len(arcs) - kept or violations > baseline_bad:
        raise RuntimeError('integer reevaluation disagrees with objective')
    record['verified_violations'] = violations
    record['candidate_extension_um'] = sum(
        node['weight'] * (positions[node['id']] - node['initial'])
        for node in nodes) / 2 / case['dbu']
    record['candidate_by_type'] = {t: sum(arc['type'] == t and g > 0
                                         for arc, g in zip(arcs, new_gaps))
                                   for t in ['A', 'B', 'C']}
    record['positions'] = positions
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('components')
    parser.add_argument('result')
    args = parser.parse_args()
    with open(args.components) as stream:
        cases = [json.loads(line) for line in stream]
    cases.sort(key=lambda case: len(case['arcs']))
    results = []
    started = time.monotonic()
    for i, case in enumerate(cases):
        if time.monotonic() - started > 120:
            print('overall diagnosis time limit reached', flush=True)
            break
        result = solve_case(case)
        results.append(result)
        if i % 20 == 0 or result.get('verified_violations', result['baseline_violations']) < result['baseline_violations']:
            print('case', i + 1, '/', len(cases), result['layer'], result['component'],
                  'baseline', result['baseline_violations'],
                  'best', result.get('verified_violations'),
                  'length', result.get('candidate_extension_um'), flush=True)
    summary = {'available_components': len(cases), 'processed_components': len(results),
               'count_optimal_components': sum(r['count_status'] == 0 for r in results),
               'length_optimal_components': sum(r.get('length_status') == 0 for r in results),
               'baseline_violations_processed': sum(r['baseline_violations'] for r in results),
               'candidate_violations_processed': sum(r.get('verified_violations', r['baseline_violations']) for r in results),
               'baseline_extension_um_processed': sum(r['baseline_extension_um'] for r in results),
               'candidate_extension_um_processed': sum(r.get('candidate_extension_um', r['baseline_extension_um']) for r in results),
               'elapsed_seconds': time.monotonic() - started}
    with open(args.result, 'w') as stream:
        json.dump({'summary': summary, 'components': results}, stream, indent=2)
        stream.write('\n')
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
