# GPU 整数后处理实现与验证

2026-10-15。已接入整数化、分组单端点搜索、双端点搜索及安全回缩，GPU 默认开启。
通俗图解见 [方案说明](gpu_postprocess_plan.md)。

本文记录初版 `-post_retract_mode single` 的结果；当前默认已改为 `cooperative`，
同输入对照及新结果见 [安全回缩优化](retraction_validation.md)。
下文的默认值和实验数据指初版单点回缩；随附 `benchmark_postprocess.tcl` 已显式选择 `single`，
便于复现历史结果。

## 使用

```tcl
delgpu -gui 0                       ;# 默认：连续 150 轮 + 完整 GPU 后处理
delgpu -gui 1                       ;# 在已启动的 GUI 中显示最终整数结果
delgpu -gui 0 -postprocess 0         ;# 对照：仅连续迭代
delgpu -gui 0 -post_sweeps 0 -post_retract_sweeps 0 ;# 对照：仅取整
```

连续参数保持 `150 / 0.25W / 35nm / adaptive 1.5→0.5`。
修复最多扫描 24 次，单端点停滞后尝试双端点，双端点最多 8 轮；回缩最多扫描 8 次。
无改善时提前结束。`-post_sweeps 0` 跳过整个单/双端点修复阶段；
`-post_pair_rounds 0` 只禁用双端点，`-post_retract_sweeps 0` 禁用回缩。
CPU 参考后端默认不执行这套后处理，显式开启会报错；`-max_iterations 0` 仍只建图。
启动脚本支持对应的 `--postprocess`、`--post-sweeps`、`--post-pair-rounds`、`--post-retract-sweeps`。

## 实现

- `Postprocess.cpp` 从原始 Vnode/ArcRange 准备 int64 几何和确定性节点分组，
  正确处理负半整数域边界；不从归一化浮点几何反推原始几何。
- `Postprocess.cuh` 在现有 stream 上执行 CUDA 核，复用现有 CSR 和连通分量。
  同组节点没有直接约束，顺序处理各组，同组节点并行。
- 单端点从关联规则边界、域边界、零位移和附近 ±1/2/4/8 DBU 产生候选，
  按 `(≥5nm 数量, 正残差数量, 残差总量, 绝对位移总量)` 严格改善才接受。
  只搜索冲突端点及其邻域，使用分离的当前/下一轮活跃标记。
- 双端点尝试各合法分支中可达到的目标距离，按移动容量、度数和端点边界分配位移。
  每条边并行计算候选，每个连通分量用一个 block 并行选择一个最优方案提交。
  评分检查两端关联边并集，共同边仅计一次。
- 回缩逐组执行，选择含当前位置的允许区间，求交集后朝零位移收缩。
  提交前逐条检查关联边残差不增加，且绝对位移严格下降。
- 整数化可能改变违例数，单调改进保证从取整初态开始。
  完整阶段扫描和运行时检查验证修复目标未变差、回缩未增加违例/残差/延长量。
  最终由独立 CPU 整数检查器直接检查原始域与所有 ArcRange，核对全部计数、最大/总残差和延长量。
- 无 GUI 时不增加中间完整坐标下载；最终每层只下载一次整数位移。
  GUI 开启时沿用连续阶段的刷新节流，后处理结束后显示最终整数结果。
  图形覆盖层的位移不写回 OpenDB 实际布线。

## 统计口径

每层在日志及 CSV 中报告 `quantized`、`repaired`、`retracted` 和 `final`：

| 字段 | 含义 |
| --- | --- |
| `conflicts` | 所有严格正残差违例的数量 |
| `remaining_5nm` | 残差 ≥5nm 的数量，包含恰好 5nm |
| `violations_lt5nm` | 严格 0<残差<5nm 的数量，不包括残差为 0 的边 |
| `max_gap_dbu` / `sum_gap_dbu` | 最大/总残差；整数阶段精确计数，总残差在连续阶段留空 |
| `extent_dbu` | 绝对 trim 位移之和 |
| `stage_ms` | 整数化/修复/回缩阶段的耗时 |

始终有 `conflicts = remaining_5nm + violations_lt5nm`。
整数阶段采用双倍 DBU 距离，`gap2 > 0` 为严格违例；
`gap2 >= ceil(10*dbu_per_micron/1000)` 为 ≥5nm，避免浮点边界误判。
连续阶段保持已有 `1e-6 DBU` 数值噪声过滤，不能把连续小残差数量的下降全部归因于搜索。
整数阶段不再使用连续势能，CSV 的 `energy`、`rule_energy` 为 0。

`postprocess_ms` 包含主机整数几何准备、分组、上传、设备缓冲申请/释放及 GPU 三阶段。
`optimization total_ms` 还包括初始化、连续迭代、最终下载和独立主机检查。
`command wall_ms` 为整个 delgpu 命令，额外包含建图；预先读库，不含读库耗时。
各阶段统计取得后统一写 CSV，因此 `elapsed_ms` 不是各后处理阶段的结束时刻，
应使用 `stage_ms` 分析阶段耗时，使用日志 `postprocess_ms` 分析完整后处理开销。

## IBEX 对照结果

输入：`flow/results/asap7/ibex/base/5_2_route.odb`，332,664 节点、437,360 条约束。
设备：RTX 5060 Laptop，CUDA 12.9.1。关闭 GUI 和建图重复校验，GPU 预热后
交替运行四个配置，各三次；四组 CSV 的 `continuous` 行逐层完全一致。
下表质量三次一致，时间取中位数；四组均独立运行连续迭代，不累加上次结果。

| 配置 | ≥5nm | 0<残差<5nm | 全部违例 | 延长量 µm | 后处理 ms | 优化总耗时 ms | 命令耗时 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 仅连续迭代 | 64 | 80,129 | 80,193 | 2693.708982 | 0 | 1868.718 | 2324.360 |
| 仅取整 | 99 | 5,070 | 5,169 | 2690.816000 | 69.335 | 1984.574 | 2432.708 |
| 取整 + 修复 | 36 | 48 | 84 | 2754.178000 | 98.693 | 2041.086 | 2510.234 |
| 完整：再做回缩 | **36** | **48** | **84** | **2632.331000** | **102.127** | **2000.836** | **2455.702** |

相对连续末态，完整后处理减少 28 条 ≥5nm 违例（43.75%），延长量减少
61.377982µm（约 2.28%）。优化总耗时中位数增加约 132ms（7.07%）；
不能用两组运行的中位数差值代替后处理自身实测的 102ms。
修复阶段允许增加延长量来减少违例，随后安全回缩收回 121.847µm，违例和残差均保持不变。
取整后 ≥5nm 暂增至 99，说明必须区分连续、取整、修复三个结果。

最终分层统计：

| 层 | ≥5nm | 0<残差<5nm | 全部违例 | 最大残差 nm | 延长量 µm |
| --- | ---: | ---: | ---: | ---: | ---: |
| M2 | 32 | 48 | 80 | 16 | 1791.877 |
| M3 | 0 | 0 | 0 | 0 | 500.685 |
| M4 | 0 | 0 | 0 | 0 | 119.147 |
| M5 | 0 | 0 | 0 | 0 | 66.912 |
| M6 | 4 | 0 | 4 | 9 | 143.441 |
| M7 | 0 | 0 | 0 | 0 | 10.269 |

全图最大残差由 45nm 降到 16nm，总残差由取整后的 7116nm 降到 432nm。
该数据库 1000 DBU/µm，DBU 与 nm 数值相同。当前移动域均向外，净延长量与绝对位移相同。

旧原版 CPU 的 200 轮加 LP/MIP 也得到过 36 条 ≥5nm，但连续输入与目标顺序不同，
这里没有接入“将同一 GPU 连续末态送入原 CPU LP/MIP”的适配器，不能据此声称质量等价或
计算 GPU 对 LP/MIP 的同输入加速比。当前验证证明的是同一 GPU 连续末态上本后处理的改善。
此方法仍是局部搜索，不保证剩余 36 条不可修复或结果全局最优。

## 验证与产物

- Bazel CUDA 宿主/模块构建通过；CPU-only entry 与 unavailable 后端构建通过。
- 72 个单元测试通过，新增 5 个后处理测试，覆盖负半整数域、半 DBU 中心、
  恰好 5nm 和非整数 nm/DBU 比例、单点局部极小值的双点修复、共享邻居和重复关联边、
  随机 A/B/C 图的全局目标与逐边回缩保证、blocked、不满足的奇偶合并、无边图和溢出拒绝。
- GUI offscreen 回归通过：迭代/后处理后图像变化，关闭 GUI 后覆盖层清理。
- 十二次测量均只下载 2,661,312 字节完整坐标（节点数 × 8），未增加中间完整下载。
- 实际普通布线长度始终为 89,786,472 DBU，确认没有写回修改 dbWire。

产物目录：`flow/reports/asap7/ibex/base/delgpu_postprocess/`，包括
`benchmark.log`、十二份 CSV、`summary.json`、`summary.md`、`gui.log` 及三张 GUI 截图。

```bash
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_postprocess.tcl \
  > flow/reports/asap7/ibex/base/delgpu_postprocess/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_postprocess.py \
  flow/reports/asap7/ibex/base/delgpu_postprocess
```
