#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Check identical repair inputs and summarize retraction quality and latency."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
log = (root / "benchmark.log").read_text()
dbu = int(re.search(r"dbu_per_micron=(\d+)", log)[1])
expected_download = int(re.search(r"DELGPU complete:.*?nodes=(\d+)", log)[1]) * 8
measurements = {}
current = None
for line in log.splitlines():
    match = re.match(r"RETRACTION sample=(\d+) name=(\w+)", line)
    if match:
        current = (match[2], int(match[1]))
    elif current and "DELGPU optimization complete:" in line:
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[current] = {k: float(fields[k]) for k in
                                 ("iteration_ms", "postprocess_ms", "total_ms",
                                  "coordinate_download_bytes")}
    elif line.startswith("RETRACTION wall_ms="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[fields["name"], int(fields["sample"])]["wall_ms"] = float(fields["wall_ms"])
summary = {}
baseline = None
for name in ("single", "cooperative", "cooperative16", "cooperative32"):
    samples = []
    times = []
    for sample in range(1, 4):
        with (root / f"{name}_{sample}.csv").open() as f:
            rows = list(csv.DictReader(f))
        repaired = [(r["layer"], *(r[k] for k in ("conflicts", "remaining_5nm", "violations_lt5nm",
                     "extent_dbu", "max_gap_dbu", "sum_gap_dbu")))
                    for r in rows if r["stage"] == "repaired"]
        if baseline is None:
            baseline = repaired
        assert repaired == baseline, f"different repair input: {name}, {sample}"
        final = [r for r in rows if r["stage"] == "final"]
        assert len(final) == len(repaired)
        for r in rows:
            assert int(r["conflicts"]) == int(r["remaining_5nm"]) + int(r["violations_lt5nm"])
        for p, f in zip((r for r in rows if r["stage"] == "repaired"), final):
            assert p["layer"] == f["layer"]
            for key in ("conflicts", "remaining_5nm", "max_gap_dbu", "sum_gap_dbu", "extent_dbu"):
                assert float(f[key]) <= float(p[key])
        quality = {
            "remaining_5nm": sum(int(r["remaining_5nm"]) for r in final),
            "violations_lt5nm": sum(int(r["violations_lt5nm"]) for r in final),
            "sum_gap_dbu": sum(float(r["sum_gap_dbu"]) for r in final),
            "max_gap_dbu": max(float(r["max_gap_dbu"]) for r in final),
            "extent_um": sum(float(r["extent_dbu"]) for r in final) / dbu,
            "layers": {r["layer"]: float(r["extent_dbu"]) / dbu for r in final},
        }
        if samples:
            assert quality == samples[0], f"nondeterministic result: {name}"
        samples.append(quality)
        measure = dict(measurements[name, sample])
        assert measure.pop("coordinate_download_bytes") == expected_download
        measure["retract_ms"] = sum(float(r["stage_ms"]) for r in rows if r["stage"] == "retracted")
        times.append(measure)
    summary[name] = dict(samples[0], **{k: statistics.median(t[k] for t in times) for k in times[0]})
(root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
lines = ["| mode | >=5nm | 0<gap<5nm | extent um | retract ms | postprocess ms | optimization ms | command ms |",
         "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
for name, s in summary.items():
    lines.append(f"| {name} | {s['remaining_5nm']} | {s['violations_lt5nm']} | {s['extent_um']:.6f} | "
                 f"{s['retract_ms']:.3f} | {s['postprocess_ms']:.3f} | {s['total_ms']:.3f} | {s['wall_ms']:.3f} |")
(root / "summary.md").write_text("\n".join(lines) + "\n")
print("\n".join(lines))
print("pass: identical repaired inputs, deterministic results, correct severe/small accounting")
