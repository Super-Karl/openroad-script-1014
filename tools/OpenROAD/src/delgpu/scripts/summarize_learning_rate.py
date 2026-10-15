#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Summarize fixed-rate screening, linear schedules and repeated short runs."""

import argparse
import csv
import json
import math
import re
import statistics
from pathlib import Path


def quality(rows, dbu, iteration=None):
    chosen = [
        r for r in rows
        if (r["stage"] == "final" if iteration is None else
            r["stage"] == "iteration" and int(r["iteration"]) == iteration)
    ]
    if len(chosen) != 6:
        raise ValueError("Expected one result per M2-M7 layer")
    return {
        "remaining_5nm": sum(int(r["remaining_5nm"]) for r in chosen),
        "extent_um": sum(float(r["extent_dbu"]) for r in chosen) / dbu,
        "layers": {r["layer"]: int(r["remaining_5nm"]) for r in chosen},
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "directory", nargs="?", type=Path,
        default=Path(__file__).resolve().parents[5]
        / "flow/reports/asap7/ibex/base/delgpu_learning_rate",
    )
    out = parser.parse_args().directory
    log = (out / "confirm.log").read_text()
    scales = set(re.findall(r"dbu_per_micron=(\d+)", log))
    assert len(scales) == 1
    dbu = int(scales.pop())
    summary = {
        "gui": False, "max_step_w": 0.25, "jump_offset_nm": 35,
        "max_backtracks": 0, "dbu_per_micron": dbu,
        "selection": "terminal state, no best-history restoration or LP/MIP",
        "timing": "three alternating warm samples; total_ms excludes graph/read_db/context",
        "sweep": {}, "anneal": {}, "confirm": {},
    }
    for file in sorted(out.glob("sweep_lr_*.csv")):
        rows = list(csv.DictReader(file.open()))
        summary["sweep"][file.stem] = {
            i: quality(rows, dbu, i) for i in (25, 50, 75, 100, 125, 150, 175, 200)
        }
    for file in sorted(out.glob("anneal_*.csv")):
        summary["anneal"][file.stem] = quality(
            list(csv.DictReader(file.open())), dbu
        )

    current = None
    for line in log.splitlines():
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
        if line.startswith("LRBENCH phase=confirm "):
            current = fields
        elif current and line.startswith("DELGPU optimization complete:"):
            current["metrics"] = {
                k: float(fields[k])
                for k in ("total_ms", "iteration_ms", "wire_extension_um")
            }
        elif current and line.startswith("LRBENCH wall_ms="):
            name, sample = current["name"], int(current["sample"])
            rate, end = float(current["rate"]), float(current["end_rate"])
            iterations = int(current["iterations"])
            rows = list(csv.DictReader((out / f"{name}_{sample}.csv").open()))
            for row in rows:
                if row["stage"] != "iteration":
                    continue
                i = int(row["iteration"])
                t = (i - 1) / (iterations - 1) if iterations > 1 else 0
                expected = rate if rate == end else (1 - t) * rate + t * end
                assert math.isclose(float(row["learning_rate"]), expected, abs_tol=1e-10)
                assert math.isclose(float(row["effective_max_step_w"]), 0.25 * expected, abs_tol=1e-10)
                assert float(row["lambda"]) == 10 * (1 << min(3, (i - 1) // 25))
            result = quality(rows, dbu)
            terminal = quality(rows, dbu, iterations)
            assert result["remaining_5nm"] == terminal["remaining_5nm"]
            result.update(current["metrics"])
            result["wall_ms"] = float(fields["wall_ms"])
            result["sample"] = sample
            data = summary["confirm"].setdefault(name, {
                "iterations": iterations, "learning_rate": rate,
                "learning_rate_end": end, "samples": [],
            })
            data["samples"].append(result)
            current = None

    for name, data in summary["confirm"].items():
        samples = data["samples"]
        assert len(samples) == 3
        assert len({s["remaining_5nm"] for s in samples}) == 1
        assert max(s["extent_um"] for s in samples) - min(s["extent_um"] for s in samples) < 1e-6
        data.update({k: samples[0][k] for k in ("remaining_5nm", "extent_um", "layers")})
        for key in ("total_ms", "iteration_ms", "wall_ms"):
            data["median_" + key] = statistics.median(s[key] for s in samples)
        print(f"{name:12s} N={data['iterations']:3d} eta={data['learning_rate']}->{data['learning_rate_end']} "
              f">=5nm={data['remaining_5nm']:3d} extension={data['extent_um']:.6f}um "
              f"optimization={data['median_total_ms']/1000:.6f}s")
    baseline = summary["confirm"]["baseline200"]
    for data in summary["confirm"].values():
        data["time_saved_percent"] = 100 * (1 - data["median_total_ms"] / baseline["median_total_ms"])
        data["extent_change_um"] = data["extent_um"] - baseline["extent_um"]
        data["remaining_5nm_change"] = data["remaining_5nm"] - baseline["remaining_5nm"]
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
