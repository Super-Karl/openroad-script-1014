#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Validate EMA ablations and summarize the three interleaved warm runs."""
import csv
import json
import re
import statistics
import sys
from pathlib import Path

root = Path(sys.argv[1])
log = (root / "benchmark.log").read_text()
assert "pass: EMA comparison complete; actual routed wire unchanged" in log
meta = re.search(r"EMA input=(.*?) dbu_per_micron=(\d+) original_wire_dbu=(\d+)", log)
assert meta
dbu = int(meta[2])
expected_bytes = int(re.search(r"DELGPU complete:.*?nodes=(\d+)", log)[1]) * 8
measurements = {}
current = None
for line in log.splitlines():
    match = re.match(r"EMA sample=(\d+) name=(\w+) iterations=(\d+)", line)
    if match:
        current = (match[2], int(match[1]))
        measurements[current] = {"iterations": int(match[3]), "lpa_ms": 0, "ema_ms": 0}
    elif current and "DELGPU optimization complete:" in line:
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[current].update({k: float(fields[k]) for k in
            ("iteration_ms", "postprocess_ms", "total_ms", "coordinate_download_bytes")})
        assert abs(float(fields["extent_um"]) - float(fields["wire_extension_um"])) < 1e-9
    elif current and "LEO timing (ms):" in line:
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[current]["lpa_ms"] += float(fields["LPA"])
        measurements[current]["ema_ms"] += float(fields["EMA"])
    elif line.startswith("EMA wall_ms="):
        fields = dict(re.findall(r"(\w+)=([^ ]+)", line))
        measurements[fields["name"], int(fields["sample"])]["wall_ms"] = float(fields["wall_ms"])

summary = {}
repair_inputs = {}
for name in ("baseline150", "ema150", "jump150", "seed150", "seed100", "seed75", "mip150", "leo"):
    qualities, times = [], []
    for sample in range(1, 4):
        with (root / f"{name}_{sample}.csv").open() as f:
            rows = list(csv.DictReader(f))
        measured = dict(measurements[name, sample])
        if name == "leo":
            final = [r for r in rows if r["layer"] != "TOTAL"]
            quality = {
                "extension_um": round(sum(float(r["extent_um"]) for r in final), 6),
                "remaining_5nm": sum(int(r["remaining_5nm"]) for r in final),
                "violations_lt5nm": sum(int(r["below_5nm"]) for r in final),
                "layers": {r["layer"]: float(r["extent_um"]) for r in final},
            }
            measured.pop("iterations")
        else:
            budget = measured.pop("iterations")
            measured.pop("lpa_ms")
            measured.pop("ema_ms")
            signature = [(r["layer"], *(r[k] for k in
                ("conflicts", "remaining_5nm", "violations_lt5nm", "extent_dbu", "max_gap_dbu", "sum_gap_dbu")))
                for r in rows if r["stage"] == "repaired"]
            if budget in repair_inputs:
                assert signature == repair_inputs[budget], (name, "repair input differs at same iteration budget")
            else:
                repair_inputs[budget] = signature
            final = [r for r in rows if r["stage"] == "final"]
            assert len(final) == len(signature) and final
            for r in rows:
                assert int(r["conflicts"]) == int(r["remaining_5nm"]) + int(r["violations_lt5nm"])
            for before, after in zip((r for r in rows if r["stage"] == "repaired"), final):
                assert before["layer"] == after["layer"]
                for key in ("conflicts", "remaining_5nm", "max_gap_dbu", "sum_gap_dbu", "extent_dbu"):
                    assert float(after[key]) <= float(before[key]), (name, key)
            quality = {
                "extension_um": sum(float(r["extent_dbu"]) for r in final) / dbu,
                "remaining_5nm": sum(int(r["remaining_5nm"]) for r in final),
                "violations_lt5nm": sum(int(r["violations_lt5nm"]) for r in final),
                "sum_gap_dbu": sum(float(r["sum_gap_dbu"]) for r in final),
                "iterations": budget,
                "layers": {r["layer"]: float(r["extent_dbu"]) / dbu for r in final},
            }
            assert measured.pop("coordinate_download_bytes") == expected_bytes
            measured["retract_ms"] = sum(float(r["stage_ms"]) for r in rows if r["stage"] == "retracted")
            measured["joint_ms"] = sum(float(r["stage_ms"]) for r in rows if r["stage"] == "joint")
        assert final
        if qualities:
            assert quality == qualities[0], (name, "nondeterministic quality")
        qualities.append(quality)
        times.append(measured)
    summary[name] = dict(qualities[0], **{k: statistics.median(t[k] for t in times) for k in times[0]})
    summary[name]["samples"] = times
result = {"input": meta[1], "dbu_per_micron": dbu, "original_wire_dbu": int(meta[3]),
          "actual_odb_wire_unchanged": True, "samples": 3, "methods": summary}
(root / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
lines = ["| 配置 | 增加量 µm | ≥5nm | 0<残差<5nm | 回缩 ms | 全后处理 ms | 命令 ms |",
         "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
for name, v in summary.items():
    lines.append(f"| {name} | {v['extension_um']:.3f} | {v['remaining_5nm']} | {v['violations_lt5nm']} | "
                 f"{v.get('retract_ms', v.get('ema_ms', 0)):.3f} | {v.get('postprocess_ms', 0):.3f} | {v['wall_ms']:.3f} |")
(root / "summary.md").write_text("\n".join(lines) + "\n")
with (root / "summary.csv").open("w", newline="") as f:
    keys = ["extension_um", "remaining_5nm", "violations_lt5nm", "iterations", "retract_ms", "joint_ms", "postprocess_ms", "total_ms", "wall_ms", "lpa_ms", "ema_ms"]
    writer = csv.writer(f)
    writer.writerow(["configuration", *keys])
    for name, v in summary.items():
        writer.writerow([name, *(v.get(k, "") for k in keys)])
print("\n".join(lines))
print("pass: repeated quality, same-budget repaired inputs, nonworsening geometry and final-download accounting validated")
