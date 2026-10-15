> 历史记录：本文对应删除额外策略之前的版本。当前实现和结果见 [论文流程基准](paper_baseline_validation.md)。

# LPA+EMA baseline 实测

本记录中的“完整流程”指当前代码的建图、LPA、EMA，非原论文全部工业流程的严格复现。
现在已将全部正残差违例作为默认统计，并在 `leo` 命令内自动输出；
算法差异和新测量口径见 [全部违例与论文复现审计](algorithm_audit.md)。

日期：2026-10-15。使用本次补全后的 LPA、EMA 实现，输入与 DEL/GPU 实验一致：
`flow/results/asap7/ibex/base/5_2_route.odb`，M2–M7，DBU/µm=1000。
建图产生 332,664 个节点、437,360 条弧。

直接运行无参数命令 `leo`，按默认流程完成建图、LPA、EMA。
没有强制设置 200 轮：LEO 是区间选择和长度压缩流程，EMA 原有上限为每子图
1000 次改进，本次所有层的 `limited=0`。LPA 和 EMA 算法保持原样。
只做了 OpenROAD 注册、当前 `web::` GUI API 适配、测试链接修复及独立只读统计。

## 结果

关闭 GUI，预热一次后测三次，质量结果完全一致：

| 指标 | LEO 完整流程末态 |
|---|---:|
| ≥5nm 违例数 | **339** |
| 所有正残差的违例数 | **340** |
| ≥5nm：A / B / C | 57 / 19 / 263 |
| trim 模型延长量 | **2441.901µm** |
| 最大残差 | **58nm** |
| 完整 `leo` 命令中位数，含建图 | **0.931854s** |
| 建图时间中位数 | 0.696815s |
| LPA 时间中位数 | 0.095210s |
| EMA 时间中位数 | 0.127037s |
| LPA+EMA 合计中位数 | **0.222397s** |

阶段中位数分别计算，不能要求其相加严格等于整次运行中位数。
完整命令时间包含原版日志和命令包装，排除读库及独立的质量评估。
三次完整命令为 0.965098 / 0.928438 / 0.931854s。

| 层 | ≥5nm | A / B / C ≥5nm | 延长量（µm） |
|---|---:|---|---:|
| M2 | 274 | 54 / 18 / 202 | 1638.376 |
| M3 | 50 | 0 / 0 / 50 | 479.907 |
| M4 | 1 | 0 / 0 / 1 | 114.132 |
| M5 | 0 | 0 / 0 / 0 | 64.653 |
| M6 | 14 | 3 / 1 / 10 | 135.323 |
| M7 | 0 | 0 / 0 / 0 | 9.510 |

初态整数计算的 ≥5nm 数量为 100,303。末态从 `pos_x2/pos_y2` 读取，
不是初始 `center_x2/center_y2`。对每条弧的所有合法区间计算最近距离，
包含未选到可行区间的弧，不以 LPA 的 `unresolved_arcs` 代替最终违例统计。
阈值按双倍 DBU 精确判断，等于 5nm 的残差计入；与浮点归一化的参考报告
比对边界值时应留意舍入方式。

延长量根据末态相对初态的沿轨位移和 `high_end` 方向计算，已逐层核对等于
EMA 的 `length_after2`。绝对位移之和在本例也与此相同。
所有最终坐标都经过硬移动域及 blocked 节点检查。
当前仍未写回实际布线，OpenDB 普通布线长度前后均为 89,786,472 DBU，
因此实际已写回的线长增量为 0；表中线长是模型延长量，违例是图约束残差。

## 与当前 GPU 版本的同环境复测

同一数据库、GUI 关闭，GPU 采用 `200 / 0.25W /35nm / max_backtracks=0`，
预热后测三次。两个程序的 stdout 日志和 GPU CSV 都先写 Linux `/tmp`，
测量后才复制到结果目录。

| 方法 | 模型延长量（µm） | ≥5nm 违例 | 完整命令中位数，含建图（s） |
|---|---:|---:|---:|
| LEO 完整 LPA+EMA | **2441.901** | 339 | **0.931854** |
| 当前 GPU，200 轮、无回溯 | 2764.297657 | **83** | 2.984963 |

GPU 三次完整命令为 2.984963 / 2.994550 / 2.977744s，优化阶段中位数为 2.526175s。
完整命令时间排除读库及 CUDA 上下文初始化；GPU 命令内部的 CSV、最终下载和主机检查
包含在内，LEO 的独立质量评估在命令外。
当前 LEO 比 GPU 更快，延长量更小；GPU 的 ≥5nm 违例更少。
不能把之前相对 DEL 连续迭代的加速比沿用为相对 LEO 的加速比。

## 日志路径对计时的影响

原版 LEO 逐子图输出数万行日志。本次先将 stdout 直接重定向到 `/mnt/e`，
三次完整命令分别为 11.542853 / 15.228746 / 12.032646s；
改为 Linux `/tmp` 后降到约 0.932s，而质量结果完全相同。
这是本次运行环境下实测的日志写入开销，未修改算法，也未关闭原版日志。
不能把前一组含 Windows 挂载盘日志开销的时间当作求解器计算耗时。
原始记录保存在 `ntfs_logging/`。

## 构建、验证和复现

Bazel GUI+CUDA 宿主构建通过；LEO 六个测试目标、39 项测试通过，包括 LPA 与 EMA。
本次补全发生在首次构建期间，随后已经重新增量构建完整版本，测量使用的是重编译后的
完整 LPA/EMA，可通过算法源码 SHA-256 记录核验。

在 ORFS 根目录执行：

```bash
./dev-openroad.sh build
leo_benchmark_dir=$(mktemp -d /tmp/leo-baseline.XXXXXX)
LEO_BENCHMARK_DIR="$leo_benchmark_dir" ./dev-openroad.sh run -no_splash -exit \
  tools/OpenROAD/src/leo/scripts/benchmark_baseline.tcl \
  > "$leo_benchmark_dir/benchmark.log" 2>&1
```

完整记录位于 `flow/reports/asap7/ibex/base/leo_baseline/`：
`benchmark.log`、`quality_1.csv` 至 `quality_3.csv`、`gpu_compare.log`、
`summary.json`、`analyze.py`、`tests.log`、`complete_algorithm_sources.json`。
