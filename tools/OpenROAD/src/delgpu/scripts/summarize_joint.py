#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Validate identical inputs, monotonic quality and summarize warm medians."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
log = (root / "benchmark.log").read_text()
assert "pass: joint comparison complete; actual routed wire unchanged" in log
meta = re.search(r"JOINT input=(.*?) dbu_per_micron=(\d+) original_wire_dbu=(\d+)", log)
assert meta
dbu = int(meta[2])
expected_download = int(re.search(r"DELGPU complete:.*?nodes=(\d+)", log)[1]) * 8
measurements = {}
current = None
for line in log.splitlines():
    match = re.match(r"JOINT sample=(\d+) name=(\w+)", line)
    if match:
        current = (match[2], int(match[1]))
    elif current and "DELGPU optimization complete:" in line:
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[current] = {k: float(fields[k]) for k in
                                 ("iteration_ms", "postprocess_ms", "total_ms", "coordinate_download_bytes")}
    elif line.startswith("JOINT wall_ms="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[fields["name"], int(fields["sample"])]["wall_ms"] = float(fields["wall_ms"])

summary = {}
repaired_input = None
baseline_input = None
for name in ("baseline", "network", "balanced", "quality"):
    qualities, times = [], []
    for sample in range(1, 4):
        with (root / f"{name}_{sample}.csv").open() as f:
            rows = list(csv.DictReader(f))
        signature = lambda stage: [(r["layer"], *(r[k] for k in
            ("conflicts", "remaining_5nm", "violations_lt5nm", "extent_dbu", "max_gap_dbu", "sum_gap_dbu")))
            for r in rows if r["stage"] == stage]
        if repaired_input is None:
            repaired_input, baseline_input = signature("repaired"), signature("retracted")
        assert signature("repaired") == repaired_input, (name, sample, "repair input changed")
        assert signature("retracted") == baseline_input, (name, sample, "joint input changed")
        final = [r for r in rows if r["stage"] == "final"]
        assert len(final) == len(repaired_input) and final
        for r in rows:
            assert int(r["conflicts"]) == int(r["remaining_5nm"]) + int(r["violations_lt5nm"])
        for before, after in zip((r for r in rows if r["stage"] == "retracted"), final):
            assert before["layer"] == after["layer"]
            for key in ("conflicts", "remaining_5nm", "max_gap_dbu", "sum_gap_dbu", "extent_dbu"):
                assert float(after[key]) <= float(before[key]), (name, key)
        quality = {
            "extension_um": sum(float(r["extent_dbu"]) for r in final) / dbu,
            "remaining_5nm": sum(int(r["remaining_5nm"]) for r in final),
            "violations_lt5nm": sum(int(r["violations_lt5nm"]) for r in final),
            "sum_gap_dbu": sum(float(r["sum_gap_dbu"]) for r in final),
            "layers": {r["layer"]: float(r["extent_dbu"]) / dbu for r in final},
        }
        if qualities:
            assert quality == qualities[0], (name, "nondeterministic quality")
        qualities.append(quality)
        time = measurements[name, sample]
        assert time.pop("coordinate_download_bytes") == expected_download
        time["joint_ms"] = sum(float(r["stage_ms"]) for r in rows if r["stage"] == "joint")
        times.append(time)
    summary[name] = dict(qualities[0], **{k: statistics.median(t[k] for t in times) for k in times[0]})
    summary[name]["samples"] = times
    summary[name]["increase_percent"] = summary[name]["extension_um"] * dbu / int(meta[3]) * 100
result = {"input": meta[1], "dbu_per_micron": dbu, "original_wire_dbu": int(meta[3]),
          "actual_odb_wire_unchanged": True, "sample_count": 3, "methods": summary}
(root / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
lines = ["| 配置 | 增加量 µm | ≥5nm | 0<残差<5nm | 联合阶段 ms | 完整后处理 ms | 优化 ms | 命令 ms |",
         "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
for name, s in summary.items():
    lines.append(f"| {name} | {s['extension_um']:.3f} | {s['remaining_5nm']} | {s['violations_lt5nm']} | "
                 f"{s['joint_ms']:.3f} | {s['postprocess_ms']:.3f} | {s['total_ms']:.3f} | {s['wall_ms']:.3f} |")
(root / "summary.md").write_text("\n".join(lines) + "\n")
with (root / "summary.csv").open("w", newline="") as f:
    writer = csv.writer(f)
    keys = [k for k in summary['baseline'] if k not in ('layers', 'samples')]
    writer.writerow(["configuration", *keys])
    for name, values in summary.items():
        writer.writerow([name, *(values[k] for k in keys)])
print("\n".join(lines))
print("pass: deterministic final quality, identical joint inputs, one final download per layer")
