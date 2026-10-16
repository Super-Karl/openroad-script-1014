#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Summarize GUI-off, same-input wire-weight CSVs and command timings."""
import csv
import json
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[5]
directory = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    root / "flow/reports/asap7/ibex/base/delgpu_wire_weight"
)
measurements = defaultdict(dict)
marker = re.compile(r"WIRE_WEIGHT weight=(\S+) sample=(\d+)(?: wall_ms=(\S+))?$")
complete = re.compile(r"(\w+)=([^ ]+)")
active = None
log = (directory / "benchmark.log").read_text()
dbu_values = set(re.findall(r"dbu_per_micron=(\d+)", log))
if len(dbu_values) != 1:
    raise ValueError("Expected exactly one database unit scale in benchmark.log")
dbu = int(dbu_values.pop())
for line in log.splitlines():
    match = marker.fullmatch(line)
    if match:
        key = (match[1], int(match[2]))
        if match[3]:
            measurements[key]["wall_ms"] = float(match[3])
            active = None
        else:
            active = key
    elif active and line.startswith("DELGPU optimization complete:"):
        data = dict(complete.findall(line))
        for column in ("iteration_ms", "postprocess_ms", "total_ms"):
            measurements[active][column] = float(data[column])

stages = ("continuous", "quantized", "repaired", "retracted", "final")
runs = defaultdict(list)
for path in directory.glob("weight_*_*.csv"):
    _, weight, sample = path.stem.split("_")
    result = {"weight": float(weight), "sample": int(sample)}
    sums = defaultdict(lambda: defaultdict(float))
    for row in csv.DictReader(path.open()):
        if float(row["wire_weight"]) != float(weight):
            raise ValueError(f"Mismatched wire weight in {path}")
        if row["stage"] in stages:
            stage = sums[row["stage"]]
            for key in ("extent_dbu", "remaining_5nm", "violations_lt5nm", "stage_ms"):
                stage[key] += float(row[key])
    if set(sums) != set(stages):
        raise ValueError(f"Incomplete report: {path}")
    for stage in stages:
        result[f"{stage}_extension_um"] = sums[stage]["extent_dbu"] / dbu
        result[f"{stage}_ge5"] = int(sums[stage]["remaining_5nm"])
        result[f"{stage}_lt5"] = int(sums[stage]["violations_lt5nm"])
    result.update(measurements[(weight, int(sample))])
    if "wall_ms" not in result:
        raise ValueError(f"Missing command timing for {path}")
    runs[float(weight)].append(result)

summary = []
for weight, values in sorted(runs.items()):
    entry = {"weight": weight, "samples": len(values)}
    for key in values[0]:
        if key not in ("weight", "sample"):
            if (key.endswith("extension_um") or key.endswith("ge5") or key.endswith("lt5")):
                if max(v[key] for v in values) - min(v[key] for v in values) > 1e-6:
                    raise ValueError(f"Nonrepeatable quality: weight={weight} column={key}")
            entry[key] = statistics.median(v[key] for v in values)
    summary.append(entry)
(directory / "summary.json").write_text(json.dumps({"summary": summary, "runs": dict(
    (str(weight), values) for weight, values in sorted(runs.items())
)}, indent=2) + "\n")
with (directory / "summary.csv").open("w", newline="") as out:
    writer = csv.DictWriter(out, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
lines = [
    "| Wire weight | Continuous +µm | Continuous ≥5nm | Continuous <5nm | Final +µm | Final ≥5nm | Final <5nm | Iteration ms | Postprocess ms | Command ms |",
    "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
]
for row in summary:
    lines.append(f"| {row['weight']:g} | {row['continuous_extension_um']:.3f} | "
                 f"{row['continuous_ge5']} | {row['continuous_lt5']} | "
                 f"{row['final_extension_um']:.3f} | {row['final_ge5']} | {row['final_lt5']} | "
                 f"{row['iteration_ms']:.3f} | {row['postprocess_ms']:.3f} | {row['wall_ms']:.3f} |")
report = "\n".join(lines) + "\n"
(directory / "summary.md").write_text(report)
print(report, end="")
