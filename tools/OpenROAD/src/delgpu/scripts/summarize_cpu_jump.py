#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Summarize continuous iteration 200 and repeated warm CPU/GPU timings."""

import argparse
import csv
import json
import re
import statistics
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "directory",
        nargs="?",
        type=Path,
        default=Path(__file__).resolve().parents[5]
        / "flow/reports/asap7/ibex/base/delgpu_cpu_jump_200",
    )
    out = parser.parse_args().directory
    text = (out / "benchmark.log").read_text()
    timings = {}
    current = None
    for line in text.splitlines():
        match = re.match(r"BENCHMARK sample=(\d+) method=(\w+)$", line)
        if match:
            current = (match[2], int(match[1]))
        elif current and line.startswith("DELGPU optimization complete:"):
            fields = dict(re.findall(r"(\w+)=([^\s]+)", line))
            timings[current] = {
                key: float(fields[key])
                for key in ("total_ms", "iteration_ms", "wire_extension_um")
            }
        elif current and line.startswith("BENCHMARK wall_ms="):
            timings[current]["wall_ms"] = float(line.split()[1].split("=")[1])
            current = None

    dbu_scales = set(re.findall(r"dbu_per_micron=(\d+)", text))
    if len(dbu_scales) != 1:
        raise ValueError(f"Inconsistent or missing DBU scale: {dbu_scales}")
    dbu = int(dbu_scales.pop())
    summary = {
        "parameters": {"iterations": 200, "max_step": 0.25, "jump_offset_nm": 35},
        "gui": False,
        "dbu_per_micron": dbu,
        "selection": "continuous iteration 200; no history restoration or LP/MIP",
        "timing": "warm GPU, three alternating samples; optimization includes initialization and host final check, excludes graph/database/context",
        "wire_metric": "outward trim displacement; original OpenDB routes are unchanged",
        "methods": {},
    }
    all_rows = {}
    for method in ("cpu", "gpu0", "gpu7", "gpu19"):
        samples = []
        for sample in range(1, 4):
            rows = list(csv.DictReader((out / f"{method}_{sample}.csv").open()))
            initial = [r for r in rows if r["stage"] == "initial"]
            iters = [r for r in rows if r["stage"] == "iteration"]
            last = [r for r in iters if r["iteration"] == "200"]
            final = [r for r in rows if r["stage"] == "final"]
            assert len(initial) == len(last) == len(final) == 6
            assert len(iters) == 1200
            assert {r["layer"]: r["remaining_5nm"] for r in last} == {
                r["layer"]: r["remaining_5nm"] for r in final
            }
            metrics = {
                "sample": sample,
                "remaining_5nm": sum(int(r["remaining_5nm"]) for r in last),
                "extent_um": sum(float(r["extent_dbu"]) for r in last) / dbu,
                "layers": {r["layer"]: int(r["remaining_5nm"]) for r in last},
                **timings[(method, sample)],
            }
            samples.append(metrics)
            all_rows[(method, sample)] = {
                (r["layer"], r["iteration"]): r for r in iters
            }
        assert len({s["remaining_5nm"] for s in samples}) == 1
        assert max(s["extent_um"] for s in samples) - min(
            s["extent_um"] for s in samples
        ) < 1e-6
        summary["methods"][method] = {
            "samples": samples,
            "remaining_5nm": samples[0]["remaining_5nm"],
            "extent_um": samples[0]["extent_um"],
            **{
                "median_" + key: statistics.median(s[key] for s in samples)
                for key in ("total_ms", "iteration_ms", "wall_ms")
            },
        }

    comparisons = []
    for sample in range(1, 4):
        cpu, gpu = all_rows[("cpu", sample)], all_rows[("gpu19", sample)]
        comparison = {
            "sample": sample,
            "pairs": len(cpu),
            "remaining_5nm_mismatches": sum(
                cpu[k]["remaining_5nm"] != gpu[k]["remaining_5nm"] for k in cpu
            ),
            "jump_count_mismatches": sum(
                cpu[k]["repairs"] != gpu[k]["repairs"] for k in cpu
            ),
        }
        assert comparison["remaining_5nm_mismatches"] == 0
        assert comparison["jump_count_mismatches"] == 0
        comparisons.append(comparison)
    summary["cpu_gpu19_comparison"] = comparisons
    cpu = summary["methods"]["cpu"]
    gpu0 = summary["methods"]["gpu0"]
    gpu19 = summary["methods"]["gpu19"]
    summary["gpu0_vs_cpu"] = {
        "optimization_speedup": cpu["median_total_ms"] / gpu0["median_total_ms"],
        "iteration_speedup": cpu["median_iteration_ms"] / gpu0["median_iteration_ms"],
        "wire_extension_difference_um": gpu0["extent_um"] - cpu["extent_um"],
        "wire_extension_increase_percent": 100 * (gpu0["extent_um"] / cpu["extent_um"] - 1),
        "remaining_5nm_difference": gpu0["remaining_5nm"] - cpu["remaining_5nm"],
    }
    summary["gpu0_vs_gpu19_time_saved_percent"] = 100 * (
        1 - gpu0["median_total_ms"] / gpu19["median_total_ms"]
    )
    (out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    for method, data in summary["methods"].items():
        print(
            f"{method:5s}: extent={data['extent_um']:.6f} um, "
            f">=5nm={data['remaining_5nm']}, "
            f"optimization={data['median_total_ms'] / 1000:.6f} s, "
            f"iteration={data['median_iteration_ms'] / 1000:.6f} s, "
            f"command={data['median_wall_ms'] / 1000:.6f} s"
        )
    print(json.dumps(summary["gpu0_vs_cpu"], indent=2))
    print("GPU0 vs GPU19 time saved (%):", summary["gpu0_vs_gpu19_time_saved_percent"])


if __name__ == "__main__":
    main()
