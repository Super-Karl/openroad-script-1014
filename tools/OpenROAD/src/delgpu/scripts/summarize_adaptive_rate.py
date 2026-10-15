#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Summarize feedback schedules and audit the recorded per-layer step sizes."""

import argparse
import csv
import json
import math
import re
import statistics
from pathlib import Path

from summarize_learning_rate import quality


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "directory", nargs="?", type=Path,
        default=Path(__file__).resolve().parents[5]
        / "flow/reports/asap7/ibex/base/delgpu_adaptive_rate",
    )
    out = parser.parse_args().directory
    log = (out / "confirm.log").read_text()
    scales = set(re.findall(r"dbu_per_micron=(\d+)", log))
    assert len(scales) == 1
    dbu = int(scales.pop())
    summary = {
        "gui": False, "max_step_w": 0.25, "jump_offset_nm": 35,
        "max_backtracks": 0, "dbu_per_micron": dbu,
        "selection": "terminal iteration; no best snapshot or LP/MIP",
        "timing": "three alternating warm samples, optimization includes initialization/final check, excludes graph/read_db/context",
        "sweep": {}, "confirm": {},
    }
    for file in sorted(out.glob("sweep_*.csv")):
        summary["sweep"][file.stem] = quality(
            list(csv.DictReader(file.open())), dbu
        )
    current = None
    for line in log.splitlines():
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if line.startswith("ADAPTIVE phase=confirm "):
            current = fields
        elif current and line.startswith("DELGPU optimization complete:"):
            current["metrics"] = {
                k: float(fields[k])
                for k in ("total_ms", "iteration_ms", "wire_extension_um")
            }
        elif current and line.startswith("ADAPTIVE wall_ms="):
            name, sample = current["name"], int(current["sample"])
            start, end = float(current["rate"]), float(current["end"])
            iterations = int(current["iterations"])
            rows = list(csv.DictReader((out / f"{name}_{sample}.csv").open()))
            final = [r for r in rows if r["stage"] == "final"]
            for layer in {r["layer"] for r in final}:
                sequence = [r for r in rows if r["stage"] == "iteration" and r["layer"] == layer]
                assert len(sequence) == iterations
                previous, cuts = start, 0
                for row in sequence:
                    i = int(row["iteration"])
                    t = (i - 1) / (iterations - 1) if iterations > 1 else 0
                    rate = float(row["learning_rate"])
                    if current["mode"] == "linear":
                        expected = start if start == end else (1 - t) * start + t * end
                        assert math.isclose(rate, expected, abs_tol=1e-10)
                    else:
                        ceiling = end + (start - end) * 0.5 * (1 + math.cos(math.pi * t))
                        assert end - 1e-10 <= rate <= min(previous, ceiling) + 1e-10
                        next_cuts = int(row["lr_feedback_reductions"])
                        assert next_cuts >= cuts
                        cuts = next_cuts
                    assert math.isclose(float(row["effective_max_step_w"]), 0.25 * rate, abs_tol=1e-10)
                    assert float(row["lambda"]) == 10 * (1 << min(3, (i - 1) // 25))
                    previous = rate
                assert math.isclose(previous, end, abs_tol=1e-10)
            result = quality(rows, dbu)
            assert result["remaining_5nm"] == quality(rows, dbu, iterations)["remaining_5nm"]
            result.update(current["metrics"])
            result.update({
                "sample": sample, "wall_ms": float(fields["wall_ms"]),
                "feedback_reductions": sum(int(r["lr_feedback_reductions"]) for r in final),
            })
            data = summary["confirm"].setdefault(name, {
                "mode": current["mode"], "iterations": iterations,
                "learning_rate": start, "learning_rate_end": end, "samples": [],
            })
            data["samples"].append(result)
            current = None
    for name, data in summary["confirm"].items():
        samples = data["samples"]
        assert len(samples) == 3
        assert len({s["remaining_5nm"] for s in samples}) == 1
        assert max(s["extent_um"] for s in samples) - min(s["extent_um"] for s in samples) < 1e-6
        data.update({k: samples[0][k] for k in ("remaining_5nm", "extent_um", "layers", "feedback_reductions")})
        for key in ("total_ms", "iteration_ms", "wall_ms"):
            data["median_" + key] = statistics.median(s[key] for s in samples)
        print(f"{name:12s}: >=5nm={data['remaining_5nm']}, extension={data['extent_um']:.6f}um, "
              f"optimization={data['median_total_ms']/1000:.6f}s, feedback_cuts={data['feedback_reductions']}")
    baseline = summary["confirm"]["fixed200"]
    for data in summary["confirm"].values():
        data["time_saved_percent"] = 100 * (1 - data["median_total_ms"] / baseline["median_total_ms"])
        data["extent_change_um"] = data["extent_um"] - baseline["extent_um"]
        data["remaining_5nm_change"] = data["remaining_5nm"] - baseline["remaining_5nm"]
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
