# DEL（Differentiable End of Line）

当前流程为：建图 → 连续优化 → 端点延伸压缩 → trim conflict 合法化 → 最终 conflict 统计。保留与 LEO 相同的 trim、Vnode、移动域和 A/B/C 约束；后处理只更新 trim 覆盖层坐标，不改变实际布线、不写入 ODB，也不调用路由器 DRC。

## 自动后处理与最终 conflict

普通 `del`、最后一轮 `n`、运行至上限的 `c` 都自动且仅执行一次后处理；`del -graph_only` 不执行。后处理通过已链接的 OR-Tools 使用 HiGHS 进行线性/整数求解；未提供 HiGHS 的构建分别回退 GLOP / SCIP。不依赖 Python 或实验目录。

1. **端点延伸压缩**：固定当前分支及逐弧残差上限，按完整连通分量协同压缩端点延伸总量。数值检查未通过时保留该分量压缩前坐标；每个分量的线性规划最多运行 1 秒，避免一次求解整个大层。
2. **DEL conflict 合法化**：在原图完整连通分量内，重新选择 merge/separate，位置落在整数 DBU 网格。优先最少精确冲突，再最小剩余残差距离总和，最后最小延伸。不能保证每条弧残差不增加，也不能保证每层长度都减少。原移动域内无解的冲突仍保留。
3. **最终统计**：按原始 A/B/C 合法区间统计所有正残差，小于 5 nm 也算 conflict。GUI 更新到最终坐标，并对这些剩余冲突画包围框；不重新从 ODB 提取 trim。

日志最后的 `DEL final trim conflicts=...` 是后处理后的总量，包含 A/B/C、`below_5nm`、三个阶段的延伸量。`nonoptimal_components` 表示未证实最优的分量数：每次整数求解最多 5 秒；超时可使用已有可行解或域内整数回退坐标，并按实际坐标统计冲突，不宣称全局最优。

后处理由后台任务调度最多 4 个工作线程，每个分量使用独立的单线程求解器；GUI 主线程继续处理事件。HiGHS 通过 OR-Tools 的 `SolveMPModel` 入口传递线程、超时和日志参数，避免 9.14 的 `MPSolver::Solve()` 接口丢失这些设置。控制台每约 5 秒输出已完成/总分量数；每层结束输出实际耗时，以及压缩/合法化的工作线程累计耗时（并行累计时间不等于实际耗时）；最终成功后一次性更新覆盖层和冲突框。数据库被释放时取消后续分量，保留单次求解的时间上限。

使用 `-report del_iterations.csv` 时，额外生成：

| 文件 | 内容 |
| --- | --- |
| `del_iterations_postprocess.csv` | 每层压缩前、压缩后、合法化后延伸量与最终精确冲突数 |
| `del_iterations_postprocess_displacements.csv` | 每个 trim 三个阶段的位移和最终坐标 |
| `del_iterations_conflicts.csv` | 最终所有未修复冲突：类型、两端 trim ID/坐标、修复距离、是否小于 5 nm |

原 `*_residuals.csv`、`*_unrepaired.csv`、`*_displacements.csv` 保留为连续迭代结束时的结果，方便比较；最终结果以新增后处理文件为准。未指定 `-report` 时只输出统计并更新覆盖层。

建图代码从 `src/leo` 复制，使用独立的 `del` 命名空间、对象和 GUI 分组，不依赖运行 `leo`。保留相同的 M2–M7 层范围、两倍 DBU 中心坐标、20W 移动上限以及 D=5W、Dt=5W、Du=3W 规则。

| 约束 | 合法中心差 `x_dst - x_src` |
| --- | --- |
| A | `[D, +∞)` |
| B | `{0} ∪ [Dt, +∞)` |
| C | `(-∞, -Du] ∪ {0} ∪ [Du, +∞)` |

候选按完整移动域搜索，与 LEO 一致。移动域也沿用 LEO 的障碍范围，仅考虑普通 dbWire 及过孔金属；blocked 节点冻结并报告。详细建图语义见 [LEO README](../leo/README.md)。

## 构建与运行

DEL 通过 **`libdel.so` 动态链接**：`Del.cpp`、`src/graph/`、`src/optimize/`、DEL 的 GUI 绘制和 SWIG/Tcl 接口全部编入共享库，对应构建目标 `del_lib`。`del` 目标只生成接口和绘制代码的 PIC 对象，供 `del_lib` 使用；OpenROAD 直接链接 `del_lib`。GUI/ODB 基础设施仍由 OpenROAD 提供。

`./del.sh` 默认只构建 `del_lib`。算法实现、`Optimizer.h`、DEL GUI 绘制、`del.i` 和 `del.tcl` 改动无需重新链接 OpenROAD。首次构建、缺少共享库、主程序使用的 DEL 头文件（`Del.h`、`MakeDel.h`、`Graph.h`、`Trim.h`、`Rules.h`）或已列出的构建接入文件更新时，脚本自动改为构建 `openroad`，避免可执行文件与共享库接口不一致。本次构建结构升级会触发一次主程序重新链接。README、测试和启动 Tcl 脚本的更新不会触发主程序重新链接。

其他 OpenROAD 模块、公共 GUI 基础设施或依赖发生改动时，可以显式要求完整增量构建：

```bash
DEL_BUILD_TARGET=openroad ./del.sh
```

仅构建 DEL 动态库（含 DEL GUI 和 Tcl 接口）：

```bash
cmake --build tools/OpenROAD/build --target del_lib -j 4
```

动态库更新后需重新启动 OpenROAD 进程；已有 GUI 进程不会自动加载新代码。

在 ORFS 根目录执行：

```bash
cmake --build tools/OpenROAD/build --target openroad -j 4
./del.sh
```

`del.sh` 转发至 `tools/OpenROAD/delRun/del.sh`，增量构建后加载现有 ASAP7 Ibex `5_2_route.odb`，启动 GUI 并停在第 0 轮（初始图）。在 GUI 的 Tcl 控制台每输入一次 `n` 并回车，推进一轮；输入 `c` 从当前轮连续运行到本次设定上限（配置的迭代轮数），不重新建图或重置状态。逐轮统计写入启动目录的 `del_iterations.csv`。布线由用户执行；数据库不存在时脚本报错，不自动运行布线。

支持 `DEL_JOBS`、`DEL_GUI_THREADS`、`DEL_ROUTE_DB`、`DEL_OPENROAD_BIN`、`DEL_BUILD_TARGET`、`DEL_MAX_ITERATIONS`（正整数）、`DEL_REPORT` 和 `FLOW_VARIANT` 环境变量。例如：

```bash
DEL_JOBS=4 DEL_MAX_ITERATIONS=100 ./del.sh
```

也可以在已加载布线数据库的 OpenROAD Tcl 控制台执行：

```tcl
# Show the initial graph, then advance one iteration per command.
del -step -max_iterations 100 -report del_iterations.csv
n
n
# Or explicitly run all iterations automatically.
del -max_iterations 100 -frame_ms 50 -report del_auto.csv
# Keep the original graph-only behavior.
del -graph_only
```

`./del.sh` 默认使用 `del -step`，输入位置是 GUI 的 Tcl 控制台，不是启动脚本的终端。每次 `n` 使用上一轮保留的坐标和求解器状态，不重新建图；每轮返回后可以缩放、切换图层并查看统计。到达上限后再次输入 `n` 或 `c` 只提示已到上限。重新执行 `del -step` 会从原始 ODB 开始；数据库重新加载后旧会话失效。手动模式不额外等待 `-frame_ms`；CSV 的累计时间不包含两次输入之间的等待，末轮完成时生成残差 CSV。

`help del` 显示参数帮助。`-frame_ms 0` 关闭帧间等待，适用于批处理；默认轮数由 `del_configure` 决定（初始 200）。重复执行从原始 ODB 重建图和锚点，不是接着上次继续迭代。`leo` 与 `del` 可分别执行。

## GUI

Display Control 中新增独立的 `DEL` 树：

```text
DEL
├── M2
│   ├── initial_trims（默认关闭）
│   ├── trims
│   ├── vnodes
│   └── arcs
│       ├── A
│       ├── B
│       └── C
├── M3
└── ... M7
```

每一轮在同一个 renderer 中更新 trim、中心点和约束连线，保留显示树的开关状态。`initial_trims` 为灰色初始轮廓，可与当前位置比较。建议只打开一个金属层并放大观察；微小位移可能小于一个显示像素，固定节点不移动，不保证每个节点每帧都有可见变化。界面只在绘制时取整到 DBU，求解器保留连续坐标。数据库释放后停止当前优化并使旧显示失效。

## 优化与评估

- 位移以本层线宽 W 归一化，锚点为初始位置，移动域及 blocked 节点保持建图定义。
- extent 使用 `alpha * (sqrt(z²+epsilon²)-epsilon)`，alpha=0.01、epsilon=0.01W。
- A 的单侧非法区域及 B 的负距离使用平方惩罚；B/C 的有限非法区间使用 `16 t²(1-t)²` 势垒，t 为区间内的归一化位置。
- 使用解析梯度、节点度数预条件、同步更新、投影和回溯；每步位移上限 0.2W。停滞时尝试可重复的小扰动，仅接受能量下降的步。
- lambda 每 25 轮递增：10、20、40、80，第 100 轮后保持 80。因此跨阶段的总能量不能直接比较；`rule_energy` 不含 lambda，可比较各轮规则势能。
- CSV 包含第 0 轮和每轮各层及 TOTAL 的能量、规则势能、extent、A/B/C 违规数、最大违规距离、最大步长、移动节点数和累计时间（包含前面帧的显示时间）。
- 违规数按连续坐标与原始合法区间精确比较，不使用接近零的容差冒充 merge。势能降低不保证违规数量同步下降，更不代表精确合法化。
- 指定 `-report path.csv` 后，结束时额外输出 `path_residuals.csv`，记录每条正残差（nm）、最近合法区域、单边连续可行性及移动域决定的残差下限。单边可行不保证整个约束图可行。
- 默认跑满指定预算，结束时报告最近 25 轮的规则势能变化；变化小可能需要调参、改变区域选择或处理不可行约束，不能直接断言增加轮次即可解决。

## GUI 违例包围框

每层增加默认开启的 `DEL/<层>/unrepaired_boxes`。对当前残差 ≥5 nm 的弧画红色空心矩形，包围其两个移动后 trim 和连线；每帧重算，残差小于 5 nm 后自动消失。显示开关独立于 `arcs/A/B/C`，可关闭普通弧线只看 debug 框。矩形按连续坐标向外取整到 DBU，框内金属仍然可见。

## 连续 3 轮停滞修复

### 5 nm 验收统计与最终冲突明细

正残差 **小于 5 nm** 的约束视为已修复；恰好 5 nm 仍属于未修复。该阈值用于验收统计、调试包围框和停滞弧筛选，不吸附坐标、不冻结 trim，也不更改梯度势能。

日志和逐轮 CSV 增加 `remaining_5nm`（仍未修复总数）、`treated_repaired_below_5nm`（正残差但按阈值视为修复的数量）和 `A/B/C_remaining_5nm`。原 `A/B/C_violations` 保留严格正残差口径，用于对照。

指定 `-report path.csv` 后，最终导出：

- `path_residuals.csv`：全部严格正残差，包含小于 5 nm 的记录。
- `path_unrepaired.csv`：仅保留残差 ≥5 nm 的未修复冲突；字段与前一份相同。
- 两份均包含 `src/dst` 节点编号、`src/dst_trim_index`、冲突 `type`、两个 trim 的最终中心坐标 `src/dst_x/y_um` 和矩形边界 `src/dst_xmin/ymin/xmax/ymax_um`。
- `repair_distance_nm` 等于 `residual_nm`，表示沿优选方向的中心差到最近合法 merge/separate 分支的距离；不是跨越全部关联约束的保证可行位移。可达性仍见 `nearest_region_reachable`、`pair_feasible` 和 `nearest_reachable_residual_nm`。
- `below_5nm` 为 1 表示小于 5 nm，0 表示不小于；`status` 对应 `treated_repaired` 或 `unrepaired`。因此未修复专用表的标记均为 0，而完整残差表可筛选两类。

坐标使用连续覆盖层位置（μm），没有按 GUI 的整数 DBU 舍入。DBU 到 nm 的换算取自输入数据库。

### 停滞跳步

每轮普通梯度更新后，为每条弧独立累计停滞轮数：该弧在更新前后均为未修复（残差 ≥5 nm），且两端前后两轮的 `llround(累计位移_DBU)` 均相同，与 GUI 的整数 DBU 显示一致。当前 Ibex 数据中 1 DBU = 1 nm；其他数据库按自身 DBU 精度判断。比较的是取整后的累计位置，而非“本轮位移是否小于 1 nm”，因此缓慢移动跨过取整边界时仍会清零计数。连续梯度位置和违例残差保持浮点精度。条件中断则清零，连续满 3 轮尝试跨势垒。

三个运行参数集中定义在 `include/del/Del.h` 的 `DelParameters`，由 C++ 全局对象 `del::global_parameters` 保存，已替代 `DEL_BARRIER_JUMP_OFFSET_NM` 宏：

| 参数 | 默认值 | 单位/含义 |
| --- | ---: | --- |
| `-max_iterations` | 200 | 本次运行的迭代上限，正整数，不再限制为 100 |
| `-max_step` | 0.2 | W 的倍数，普通梯度单步最大位移；回溯时可更小 |
| `-jump_offset_nm` | 5.0 | nm，跨势垒目标中心差相对中点的偏移 |

在 OpenROAD Tcl 控制台执行：

```tcl
del_configure                         ;# 打印并返回当前全局配置
del_configure -max_iterations 300 -max_step 0.4 -jump_offset_nm 10
del -step                             ;# 使用上述配置开始新会话
n
c
# 也可以在启动时设置，参数会成为之后运行的新默认值：
del -step -max_iterations 200 -max_step 0.3 -jump_offset_nm 8
```

参数在当前进程内保留，不写入磁盘；重启后恢复默认值。每次开始运行会打印实际参数。已开始的会话保存参数快照，`n`/`c` 沿用快照；在两步之间修改全局配置，只影响下次 `del`。重新执行 `del -step` 会重新建图并从第 0 轮开始。非法参数不会部分修改全局配置。

`./del.sh` 支持 `DEL_MAX_ITERATIONS`、`DEL_MAX_STEP`、`DEL_JUMP_OFFSET_NM` 环境变量覆盖；未设置时使用全局默认值。步长与偏移必须为有限正数，迭代次数范围为 1–2147483647。跨势垒跳转仍由两端分担、受移动域限制，不受普通梯度步长上限限制。偏移过大而两端无法联合到达时，跳转会跳过。

令 `d = x_dst - x_src`，B 的有限势垒中点为 `Dt/2`，C 为当前有限势垒所在区间的 `-Du/2` 或 `Du/2`。若当前 `d` 在中点左侧，目标中心差设为 `中点 + 偏移量`；右侧则设为 `中点 - 偏移量`。恰在中点时优先正侧，无法到达时尝试负侧。偏移量默认 5 nm，按数据库 DBU 比例换算。

- 两端共同调整中心差：按 `1 + 关联弧数` 作为各端阻力，优先按阻力倒数分配位移；关联弧越多的一端优先少移动。阻力相等时各承担一半。两端移动方向相反；一端可用移动距离不足时，由另一端补足；固定/blocked 端保持不动。两端联合可用距离不足时均不移动。整对候选检查通过后同时提交。详见 [按关联弧阻力分配方案](plan/degree-weighted-barrier-jump.md)。
- A 势能及 B 的负距离惩罚只有单侧斜坡，没有有限势垒中点，跳过跳步，继续由梯度优化处理。
- 按弧顺序执行。一次跳转后，分别清零两个实际移动端点的全部关联弧计数，避免同一轮重复移动任一端点。无法跳转的弧计数封顶为 3，下轮满足条件时继续尝试。
- 本策略替代旧的“最近分支不可达 + 所有关联约束同时合法”的吸附策略。跳步只要求跨过中点并保持在移动域内，不要求立即合法或总势能下降，可以超过普通梯度的 `0.2W` 步长；新位置从下一轮开始继续梯度优化。
- 日志的 `stall_triggered` / `stall_jumped` / `stall_unresolved` 分别表示尝试弧数、成功跨势垒数、无法跨越数。CSV 增加 `stall_jumped`，旧字段 `stall_repaired` 保留为同值兼容别名，不能再解释为“已经合法化的 trim 数”。`max_repair_um` 为最大的单端跳距；一条弧移动两个 trim 仍计一次 `stall_jumped`。
- `n`、`c` 和自动运行共用同一套计数；重新 `del -step` 会重置。

[此前停滞修复实验](../../res/del/ibex_100_stall_20260922/result.md) 使用旧吸附策略，不代表本策略的结果。

指定 `-report` 时，还会输出 `_displacements.csv`，记录最终各 trim 的初始位置、有符号位移、向外延伸及向内回缩量（μm），用于估算端点延伸代价。`del::regular_wire_length_dbu_cmd` 可读取普通 dbWire 总路径长度（DBU，不含 special wires），用于核对实际数据库线长；DEL 没有将覆盖层位移写回布线。

## 代码与测试

- `include/del/`：工具入口、trim、节点、移动域、规则和约束类型。
- `src/graph/`：与 LEO 同源的建图实现。
- `src/Del.cpp`：逐层建图、统计和优化调度。
- `src/optimize/`：纯连续求解器、逐轮报告和帧回调。
- `src/OptimizationGraphics.cpp`：动态覆盖层；`getLayerNodes()` 保留初始图，`getDisplacements()` 保存末帧 DBU 连续位移。
- `src/*Graphics*`、`src/del.i`、`src/del.tcl`：GUI 及命令桥接。
- `test/`：从 LEO 同步的几何、节点、域和 A/B/C 测试，已分别注册 CMake 和 Bazel。

```bash
cmake --build tools/OpenROAD/build --target TestDelTrims TestDelVnodes TestDelDomains TestDelArcs TestDelOptimizer TestDelUnrepaired -j 4
ctest --test-dir tools/OpenROAD/build -R '^Del(TrimGeometry|Vnodes|Domains|Arcs|Optimizer|UnrepairedGraphics)\.' --output-on-failure
```

按关联弧数加权的跨势垒分配已通过 19 项优化器测试及 `optimizer_run.tcl` 集成回归。相同 Ibex ODB 和参数下的 200 轮均分/加权对照结果见 [实验报告](../../res/del/ibex_200_degree_weighted_20260928/comparison.md)；加权版的 ≥5 nm 残留数在本次实验中略高于均分版。

后续 [39 组参数扫描](../../res/del/ibex_parameter_sweep_20260928/result.md) 固定加权算法及 200 轮预算，已测试组合中 `max_step=0.25W`、`jump_offset_nm=35` 最好：≥5 nm 残留从默认组的 502 条降到 24 条，最后 25 轮为 24–26 条。该结论仅适用于本次 Ibex 输入与测试范围，默认值保持不变。可用 `del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35` 后输入 `c` 复查。

首次 100 轮数值结果见 [Ibex 评估](../../docs/del/experiments/ibex_100.md)。

完整后续计划见 [execution-plan.md](../../docs/del/execution-plan.md)。

## 残差分布

对最终逐条残差分桶、统计分位数及单边不可行数量：

```bash
python3 tools/OpenROAD/src/del/scripts/residual_summary.py \
  del_iterations_residuals.csv --output del_residual_summary.md
```

输出 Markdown 汇总及同名 `_histogram.csv`。残差区间采用左开右闭边界；分桶不改变严格合法性判定。完整案例见 [Ibex 残差分析](../../docs/del/experiments/ibex_100_residuals.md)。

运行时全局参数接口本次仅完成代码修改，未编译或运行测试。
