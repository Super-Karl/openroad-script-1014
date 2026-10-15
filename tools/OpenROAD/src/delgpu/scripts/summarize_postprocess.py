#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Validate stage accounting and summarize the repeated postprocess benchmark."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
measurements = {}
current = None
log = (root / "benchmark.log").read_text()
units = re.search(r"dbu_per_micron=(\d+)", log)
nodes = re.search(r"DELGPU complete:.*?nodes=(\d+)", log)
assert units and nodes, "missing design units or graph size"
dbu_per_micron = int(units[1])
expected_download = int(nodes[1]) * 8
for line in log.splitlines():
    match = re.fullmatch(r"POSTPROCESS sample=(\d+) name=(\w+)", line)
    if match:
        current = (match[2], int(match[1]))
    elif current and "DELGPU optimization complete:" in line:
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[current] = {key: float(fields[key]) for key in
                                 ("iteration_ms", "postprocess_ms", "total_ms",
                                  "coordinate_download_bytes")}
    elif line.startswith("POSTPROCESS wall_ms="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[(fields["name"], int(fields["sample"]))]["wall_ms"] = float(fields["wall_ms"])

baseline = None
summary = {}
for name in ("continuous", "quantized", "repaired", "full"):
    samples = []
    for sample in range(1, 4):
        with (root / f"{name}_{sample}.csv").open() as stream:
            rows = list(csv.DictReader(stream))
        continuous = [(r["layer"], *(r[k] for k in
                      ("conflicts", "remaining_5nm", "extent_dbu", "energy")))
                      for r in rows if r["stage"] == "continuous"]
        if baseline is None:
            baseline = continuous
        assert continuous == baseline, f"different continuous input: {name}, {sample}"
        stages = {}
        for r in rows:
            assert int(r["conflicts"]) == int(r["remaining_5nm"]) + int(r["violations_lt5nm"])
            if r["stage"] in ("quantized", "repaired", "retracted", "final"):
                stages.setdefault(r["stage"], []).append(r)
        for stage, values in stages.items():
            assert len(values) == len(continuous), (name, sample, stage)
        def totals(stage):
            values = stages[stage]
            return {
                "conflicts": sum(int(r["conflicts"]) for r in values),
                "remaining_5nm": sum(int(r["remaining_5nm"]) for r in values),
                "violations_lt5nm": sum(int(r["violations_lt5nm"]) for r in values),
                "extent_um": sum(float(r["extent_dbu"]) for r in values) / dbu_per_micron,
                "max_gap_dbu": max(float(r["max_gap_dbu"]) for r in values),
                "sum_gap_dbu": (sum(float(r["sum_gap_dbu"]) for r in values)
                                if all(r["sum_gap_dbu"] for r in values) else None),
            }
        def objective(s):
            return s["remaining_5nm"], s["conflicts"], s["sum_gap_dbu"], s["extent_um"]
        if name != "continuous":
            q, p, r = (totals(s) for s in ("quantized", "repaired", "retracted"))
            assert objective(p) <= objective(q)
            assert objective(r) <= objective(p)
            assert r["sum_gap_dbu"] <= p["sum_gap_dbu"]
            assert r["max_gap_dbu"] <= p["max_gap_dbu"]
        quality = totals("final")
        if samples:
            assert quality == samples[0], f"nondeterministic quality: {name}"
        samples.append(quality)
        assert measurements[name, sample]["coordinate_download_bytes"] == expected_download
    summary[name] = dict(samples[0], **{
        k: statistics.median(measurements[name, s][k] for s in range(1, 4))
        for k in ("iteration_ms", "postprocess_ms", "total_ms", "wall_ms")
    })
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
lines = ["| case | >=5nm | 0<gap<5nm | extent um | postprocess ms | optimization ms | command ms |",
         "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
for name, s in summary.items():
    lines.append(f"| {name} | {s['remaining_5nm']} | {s['violations_lt5nm']} | "
                 f"{s['extent_um']:.6f} | {s['postprocess_ms']:.3f} | {s['total_ms']:.3f} | {s['wall_ms']:.3f} |")
(root / "summary.md").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
print("pass: all runs have identical continuous inputs, correct accounting and deterministic final quality")
