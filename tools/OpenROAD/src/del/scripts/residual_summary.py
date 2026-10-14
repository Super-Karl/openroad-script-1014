#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
"""Summarize DEL positive residuals; binning does not change legality."""
import argparse
import bisect
import csv
import math
from pathlib import Path


def quantile(values, fraction):
    if not values:
        return 0.0
    index = (len(values) - 1) * fraction
    low = int(index)
    high = min(low + 1, len(values) - 1)
    return values[low] + (values[high] - values[low]) * (index - low)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("residuals", type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="Markdown summary; a sibling histogram CSV is also written")
    args = parser.parse_args()
    with args.residuals.open() as stream:
        rows = list(csv.DictReader(stream))
    edges = [1e-6, .01, .1, .5, 1, 2, 5, 10, 20, math.inf]
    labels = ["(0, 0.000001]", "(0.000001, 0.01]", "(0.01, 0.1]",
              "(0.1, 0.5]", "(0.5, 1]", "(1, 2]", "(2, 5]",
              "(5, 10]", "(10, 20]", ">20"]
    counts = {t: [0] * len(edges) for t in "ABC"}
    for row in rows:
        counts[row["type"]][bisect.bisect_left(edges, float(row["residual_nm"]))] += 1
    n = len(rows)
    lines = ["# DEL：最终违例残差分布", "",
             "残差定义：当前有符号中心差到该弧合法区间并集的最短距离，单位 nm。"
             "严格沿用求解器的浮点计算和 `gap > 0` 口径；分桶仅用于诊断，不把小残差判为合法。", "",
             f"精确违例合计：**{n:,}**。", "",
             "| 残差区间（nm） | A | B | C | 合计 | 占剩余违例 |",
             "| --- | ---: | ---: | ---: | ---: | ---: |"]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    histogram = args.output.with_name(args.output.stem + "_histogram.csv")
    with histogram.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["interval_nm", "A", "B", "C", "total", "percent_of_violations"])
        for i, label in enumerate(labels):
            bins = [counts[t][i] for t in "ABC"]
            total = sum(bins)
            pct = 100 * total / n if n else 0
            lines.append(f"| {label} | {bins[0]:,} | {bins[1]:,} | {bins[2]:,} | {total:,} | {pct:.2f}% |")
            writer.writerow([label, *bins, total, pct])
    lines += ["", "## 分位数（仅统计正残差）", "",
              "| 类型 | 数量 | P50（nm） | P90 | P95 | P99 | 最大值 |",
              "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for t in ("A", "B", "C", "TOTAL"):
        values = sorted(float(r["residual_nm"]) for r in rows if t == "TOTAL" or r["type"] == t)
        q = [quantile(values, p) for p in (.5, .9, .95, .99, 1)]
        lines.append(f"| {t} | {len(values):,} | " + " | ".join(f"{v:.9g}" for v in q) + " |")
    lines += ["", "## 单条约束在移动域内的可行性", "",
              "`pair_feasible` 只检查这一对节点的可达中心差与合法区间是否相交；"
              "相交不保证全部约束能够同时满足。下限是移动域决定的最小可能残差。", "",
              "| 类型 | 单边不可行 | 单边有可行区间 |", "| --- | ---: | ---: |"]
    for t in ("A", "B", "C", "TOTAL"):
        group = [r for r in rows if t == "TOTAL" or r["type"] == t]
        impossible = sum(r["pair_feasible"] == "0" for r in group)
        lines.append(f"| {t} | {impossible:,} | {len(group) - impossible:,} |")
    lines += ["", "## 最近合法区域", "",
              "| 最近区域 | 数量 | 其中该区域在移动域内不可达 |", "| --- | ---: | ---: |"]
    for region in ("merge", "left_separate", "right_separate"):
        group = [r for r in rows if r["nearest_region"] == region]
        unreachable = sum(r["nearest_region_reachable"] == "0" for r in group)
        lines.append(f"| {region} | {len(group):,} | {unreachable:,} |")
    lines += ["", "## 各层剩余违例", "",
              "| 层 | 数量 | ≤1nm | >1nm | 单边不可行 |", "| --- | ---: | ---: | ---: | ---: |"]
    for layer in sorted({r["layer"] for r in rows}):
        group = [r for r in rows if r["layer"] == layer]
        small = sum(float(r["residual_nm"]) <= 1 for r in group)
        impossible = sum(r["pair_feasible"] == "0" for r in group)
        lines.append(f"| {layer} | {len(group):,} | {small:,} | {len(group)-small:,} | {impossible:,} |")
    lines += ["", "## 最大残差示例", "",
              "| 层 | arc | 类型 | src → dst | 残差（nm） | 移动域残差下限（nm） | 单边可行 |",
              "| --- | ---: | --- | --- | ---: | ---: | --- |"]
    for r in sorted(rows, key=lambda r: float(r["residual_nm"]), reverse=True)[:15]:
        lines.append(f'| {r["layer"]} | {r["arc_id"]} | {r["type"]} | {r["src"]} → {r["dst"]} | '
                     f'{float(r["residual_nm"]):.6g} | {float(r["domain_residual_floor_nm"]):.6g} | '
                     f'{"是" if r["pair_feasible"] == "1" else "否"} |')
    args.output.write_text("\n".join(lines) + "\n")
    print(args.output)
    print(histogram)


if __name__ == "__main__":
    main()
