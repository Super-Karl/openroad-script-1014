# 旧实现说明存档

这是修改 `P=k·Dt`、LPA/EMA 策略之前的 README，仅用于追溯，
其中算法、构建说明和历史数据不代表当前版本。当前说明见 [README](../README.md)。

# LEO（Line End Optimizer）

当前 ibex 的主统计为 **全部正残差违例 340 条（A=57、B=19、C=264）**，
其中 ≥5nm 为 339 条，模型延长量 2441.901µm。
`leo` 默认打印全部违例，`leo -report /tmp/leo.csv` 保存分类统计。
统计包含未接受区间的弧，小于 5nm 的正残差也计入。
自动统计后的完整命令中位数约 0.970s；原 LPA+EMA 阶段约 0.222s。
详见 [全部违例与论文复现审计](doc/algorithm_audit.md)，
以及 [先前基准验证记录](doc/baseline_validation.md)。
当前工作区也已接入 Bazel，可在 ORFS 根目录用 `./dev-openroad.sh build` 构建，
并运行 `tools/OpenROAD/src/leo/scripts/benchmark_baseline.tcl`。原有 CMake 构建说明保留如下。

LEO 是 OpenROAD 内置工具，在详细布线之后执行线端优化。目前已实现只读的初始 trim 生成、由 trim 建立的 Vnode（约束图节点）、节点移动域 `L`/`U`、A/B/C 约束弧、子图划分、LPA（最长路算法）与 EMA（延长线压缩），以及 trim/vnode/弧/移动结果的 GUI 显示；剪枝、坐标回写与最终几何复核尚未实现。

## 构建

在已经配置好 CMake 的 `tools/OpenROAD/` 目录下执行：

```bash
cmake --build build --target openroad -j 4
build/bin/openroad
```

修改源码后需要重新编译并启动新进程，已经运行的旧 OpenROAD 不会自动加载新命令。

仓库根目录的 `leo.sh` 将上述步骤串起来：默认增量编译 `openroad`，复用
`flow/results/asap7/ibex/base/5_2_route.odb`，然后用构建目录中的 OpenROAD
启动 GUI。若需要强制重跑步骤 5，可执行 `LEO_REBUILD_ROUTE=1 ./leo.sh`；可用
`LEO_JOBS=4` 调整编译并行度。

LEO 的算法实现编译为 `libleo.so`。第一次切换到该结构时需要重新链接一次
OpenROAD；之后修改 `src/leo/src/graph/`、`src/leo/src/lpa/` 或
`src/leo/src/ema/` 下的实现，只需重新生成共享库，已有 OpenROAD 可执行文件
即可加载新的算法库。修改 `Leo.h`、Tcl/SWIG 接口或 OpenROAD 接入代码时，仍需
重新链接 OpenROAD。

因此 `leo.sh` 在共享库已经存在时默认执行：

```bash
cmake --build tools/OpenROAD/build --target leo -j "${LEO_JOBS:-16}"
```

只有首次构建、缺少 `openroad` 或缺少 `libleo.so` 时才执行 `--target openroad`。
如果修改了 `OpenRoad.cc`、`Leo.h` 或 Tcl/SWIG 接口，显式执行
`LEO_BUILD_TARGET=openroad ./leo.sh`。

`leo.sh` 启动的 GUI 会在 `read_db` 完成后自动执行一次 `leo`。如果只想打开
数据库而不自动执行算法，可以直接使用 OpenROAD 启动，不调用该 Tcl 脚本，或
在脚本中暂时注释 `open_route.tcl` 末尾的 `leo`。

build graph 的第一步现在先执行 `init()`，通过当前 OpenDB 输出设计基本统计，
例如设计名、实例数、网络数、ITerm/BTerm/总 pin 数、已有布线网数、特殊线对象
和特殊线盒子数量、工艺布线层数、当前允许布线层范围以及 DBU 单位。该统计只读
数据库，不修改版图；后续建图逻辑将在同一函数之后继续展开。

## 使用

在包含本模块的新版本 OpenROAD Tcl 控制台或脚本中执行：

```tcl
leo
```

输出：

```text
LEO layer M2: begin
LEO trims: layer=M2 width=...um height=...um count=...
LEO vnodes: layer=M2 count=... D=...um Dt=...um Du=...um (5x/5x/3x width)
LEO domains: layer=M2 limit=...um capped=... blocked=... max_move=...um
LEO subgraphs: layer=M2 total=... with_arcs=...
LEO arcs: layer=M2 A=... B=... C=... total=...
LEO LPA: layer=M2 subgraph=1/21298 nodes=5 arcs=4 status=solved moved=2 added_length=0.061000um
...（每个含弧的子图一行）
LEO LPA: layer=M2 subgraphs=... solved=... partial=... invalid=... arc_free=... unresolved_arcs=... moved=... added_length=...um
LEO step 1/3: layer=M2 <trim count> trims -> <node count> vnodes -> <arc count> arcs
LEO EMA: layer=M2 subgraph=... nodes=... arcs=... iterations=... status=improved length=...->...um
...（只打印发生移动的子图）
LEO EMA: layer=M2 subgraphs=... improved=... unchanged=... limited=... moves=... length_before=...um length_after=...um saved=...um
LEO timing (ms): layer=M2 buildGraph=... LPA=... EMA=... total=...
...
LEO vnodes: layers=6 total=... trims_total=...
LEO domains: layers=6 blocked=...
LEO arcs: layers=6 A=... B=... C=... total=...
LEO total timing (ms): layers=6 total=... run=...
```

三个阶段通过 OpenROAD 的 `utl::Logger::report()` 输出，终端和 GUI 的 Scripting 窗口均可显示，也支持日志文件及 `tee` 捕获。不要改用 `std::cout`，它不会进入 GUI 的日志窗口。

命令支持可选 `-report csv_file` 和 `help leo`，默认打印全部违例统计。
当前框架可以在未加载设计时执行。后续算法的预期调用位置为：

```tcl
# After loading the design and completing the required routing setup:
detailed_route
leo
```

也可以先 `read_db` 加载详细布线后的 ODB，再调用 `leo`。模块注册不会自动在每次 `detailed_route` 后执行，需要在脚本中显式调用。

## 代码布局

| 路径 | 职责 |
| --- | --- |
| `include/leo/Leo.h` | 工具类及三个阶段入口 |
| `include/leo/MakeLeo.h`、`src/MakeLeo.cpp` | Tcl/SWIG 初始化 |
| `src/leo.tcl`、`src/leo.i` | 用户命令、参数检查和 C++ 调用桥接 |
| `src/Leo.cpp` | 按顺序调用 build graph、LPA、EMA |
| `include/leo/Rules.h` | 按层宽倍率定义的 D/Dt/Du 规则 |
| `include/leo/Graph.h` | `Vnode`、`Arc`、`Component`、`LayerNodes` 及节点中心距离 |
| `include/leo/Lpa.h` | LPA 接口与每个子图的结果 |
| `include/leo/Ema.h` | EMA 接口与每个子图的结果 |
| `src/graph/Vnodes.cpp` | 由 trim 建立 Vnode、节点排序与编号 |
| `src/graph/Domains.cpp` | 由 dbWire 金属障碍计算节点移动域 L/U（含 20W 上限） |
| `src/graph/Arcs.cpp` | 同轨 / 邻轨候选搜索与 A/B/C 弧生成 |
| `src/graph/Components.cpp` | 弧的弱连通分量（子图）划分与稳定编号 |
| `src/lpa/DifferenceConstraints.{h,cpp}` | 差分约束传播、试加与回滚 |
| `src/lpa/Lpa.cpp` | LPA 主流程（区间贪心选择）与阶段报告 |
| `src/ema/TightTree.{h,cpp}` | 紧闭包、步长与目标变化 |
| `src/ema/Ema.cpp` | EMA 主流程（pivot 扫描与移动提交）与阶段报告 |
| `src/graph/BuildGraph.cpp` | 数据库初始化、trim 生成、Vnode 与约束弧建立 |
| `src/GraphGraphics.cpp` | Vnode 中心点、约束弧与移动结果的 GUI 显示 |
| `CMakeLists.txt`、`BUILD` | CMake 与 Bazel 构建注册 |

OpenROAD 管理 `leo::Leo` 实例，通过 `ord::OpenRoad::getLeo()` 获取。后续复现代码统一放在本模块；原复现计划中的独立 `leo_cli` 布局调整为此处的 OpenROAD 内置模块，三阶段算法划分不变。

## 显示图层

加载已布线 ODB 后执行 `leo`，会在 Display Control 中新增一棵 **LEO** 显示树，都是 GUI
覆盖层，不新增 OpenDB 工艺 routing/cut 层，不修改原布线，也不随 `write_db` 写入数据库；
重新加载后执行 `leo` 即可重建。

```text
LEO
├── M2
│   ├── trims        trim 矩形
│   ├── vnodes       Vnode 的 trim 中心点（固定屏幕大小的十字）
│   ├── arcs
│   │   ├── A / B / C 该层由对应规则建立的弧，画成两端节点中心的连线
│   └── moves        LPA+EMA 后的 trim 矩形，以及从原始中心指向最终中心的箭头
├── M3
└── ...
```

勾选/取消中间节点即可整层开关（`LEO` 一键全开全关，`M2` 一键开关该层），单个规则、
trims 和 vnodes 也可单独切换。即使某层某类弧为空（如 `M7/arcs/A`）也保留开关。
弧的连线方向为 `src → dst`，暂不画箭头。

多级显示控制名依赖 `src/gui/src/displayControls.cpp` 的一处扩展：渲染器的控件名允许用
`/` 分层，注册时按层创建中间节点，中间节点沿用 GUI 原有的"勾选即切换所有子项"
机制。原有只含单级名的渲染器不受影响。注意 `gui::set_display_controls` 等按路径寻址的
Tcl 接口目前只能定位叶子控件（沿用了 GUI 原有的 `collectControls` 行为），
中间节点请在界面上点击。

每层取 `dbTechLayer::getWidth()` 为 w：沿该层优选布线方向宽度为 w，垂直方向
高度为 2w。水平层矩形为 w×2w，垂直层为 2w×w。trim 靠近导线的一边与实际
金属端边贴齐，整体放在线外，横向对准端边中心；半 DBU 中点向下取整，尺寸保持不变。

生成范围为普通信号/时钟 `dbWire` 的金属及其过孔金属包围矩形，按 net、layer 求并集，
去除重叠和内部拼接边；仅对与层优选方向一致的矩形端面上的外露部分生成 trim。
非优选方向 jog 的侧边不作为线端。单元内部 pin、特殊线和电源网络不生成 trim。
显示阶段尚未检查 trim 与其他网络、引脚、边界或其他 trim 的冲突，重叠位置也不移动。

重复执行替换上一次显示；数据库释放后旧显示失效。终端和 GUI 会输出各层的宽度、
高度和数量。`src/graph/TrimGeometry.cpp` 负责几何，`src/TrimGraphics.cpp` 负责显示。

## Vnode（约束图节点）

每个 trim 对应一个 Vnode，即约束图中的节点 v_i，因此每层节点数与 trim 数相同
（ASAP7 Ibex `5_2_route.odb` 上 M2–M7 共 318110 个）。Vnode 保留所属 net、
对应的 trim 矩形及其在 `trims_` 中的下标，便于回溯。

节点位置取 trim 的中心点。中心点可能落在半个 DBU 上（trim 尺寸为奇数时），
因此内部保存的是两倍中心坐标 `center_x2 = xMin + xMax`、`center_y2 = yMin + yMax`；
`centerDistance2()` 返回两倍中心距，可直接与两倍阈值精确比较，`centerDistance()`
返回 DBU 浮点值供报告使用。`centerOffset()` 进一步给出沿层优选方向的偏移 `ds2`
和跨轨道偏移 `dt2`。

规则按每层基本线宽缩放，倍率集中定义在 [include/leo/Rules.h](include/leo/Rules.h)：

| 规则 | 倍率常量 | ASAP7 取值 | 用途（后续约束弧） |
| --- | --- | --- | --- |
| D | `kDWidthMultiplier = 5` | 5 × width | A 类：同轨最小间距 / 最小金属长度 |
| Dt | `kDtWidthMultiplier = 5` | 5 × width | B 类：同轨 trim 合并或分离 |
| Du | `kDuWidthMultiplier = 3` | 3 × width | C 类：相邻轨 trim 合并或分离 |
| 移动上限 | `kMoveLimitWidthMultiplier = 20` | 20 × width | 节点移动域的封顶值 |

修改倍率只需改这几个常量，所有层同时生效；`makeLayerRules(width)` 按层线宽算出
DBU 阈值。例如 M2 线宽 18nm 时 D = Dt = 90nm、Du = 54nm，M6 线宽 32nm 时
D = Dt = 160nm、Du = 96nm。

节点 ID 在层内沿优选布线方向排序（水平层按 x、垂直层按 y），中心点相同的节点按
net 和 trim 下标排序，因此 ID 只取决于几何，不依赖数据库的 net 遍历顺序；重跑
`leo` 会重建同一批 ID。建图按层进行，每层一组 `LayerNodes`，通过
`Leo::getLayerNodes()` 获取。剪枝和连通分量尚未实现。

## 移动域（L/U）

每个节点有一个沿优选方向的滑动域 `[low2, up2]`，`low2 = 中心 - l_move2`、
`up2 = 中心 + u_move2`，其中 `l_move2`/`u_move2` 是 trim 向下/向上的最大可移动距离。
计算方式（`src/graph/Domains.cpp`）：

1. 障碍只取 dbWire 的金属（所有 net 的普通布线及其过孔金属包围矩形，含节点自己
   所属的 net），不取进 ITerm/BTerm pin、obstruction、blockage 和 die 边界。
2. 只看与该 trim 横向覆盖范围（跨轨 ±w）相交的障碍；相邻轨的金属不进入本轨的域，
   它们之间的相互作用由 C 类弧表达。
3. 向下/向上分别取最近一块金属的高/低边，得到 trim 盒子还能滑动的距离：
   `l_move2 = box_low - 最近下方金属的高边`、`u_move2 = 最近上方金属的低边 - box_high`。
   自己的金属就是最近的那块，所以高端 trim 的 `l_move2` 恒为 0、低端 trim 的
   `u_move2` 恒为 0 —— 不缩短核心、只能向外延伸，这条自动成立。
4. 两个方向都按 `kMoveLimitWidthMultiplier = 20`（20 × width）封顶；
   ASAP7 上 M2–M3 上限 0.36um，M4–M5 上限 0.48um，M6–M7 上限 0.64um。
5. trim 当前已经压在固定金属上（gap 比 trim 窄）时记 `blocked`，域退化为当前点，
   不做修补，等后续阶段按输入冲突报告。

ASAP7 Ibex 结果：全部 318110 个节点中只有 4 个 `blocked`（都在 M2）；被 20W 上限
截断的节点占多数（M2 59%、M3 72%、M4 86%、M5 90%、M6 82%、M7 94%），说明多数线端
两侧的空间比上限更大，域的实际宽度由上限决定。

## LPA（最长路算法）

第二步按弱连通分量（子图）求解，`Leo::runLpa` 调用 `solveLpa()`。子图在 build graph
阶段由 `collectComponents()` 划分：所有弧连接其两个端点，孤立节点单独成子图，按最小
节点 ID 稳定编号。

每个子图内的处理顺序（依据论文 §4.2.1 与计划 02）：

1. **松弛模型定序**：把每条弧的合法区间取凸包（C 类弧凸包为全体实数，等于被松弛，
   与论文图 8 一致），加上域边界后传播，得到 `x_lower/x_upper`；模型本身不可行时退回
   用域边界排序。弧按 `(类型 A→B→C, 初始 slack, 弧 ID)` 排序。
2. **区间排序**：`delta_ideal = ideal[dst] - ideal[src]`，`ideal` 取该节点延伸量最小的
   位置（高端 trim 取 `L`，低端取 `U`，在单侧域里就是当前位置）。区间按到 `delta_ideal`
   的距离从小到大试。
3. **试加与回滚**：区间转成 `x_dst >= x_src + d` 的差分边；从**域下界**起推（这是完备的
   可行性判据：逐点最小解越上界即为不可行），成功后保留，失败则精确回滚该次试加。
   所有区间都不可行时记为 unresolved 弧。
4. **取坐标**：从理想位置出发，按已接受的区间把节点沿"只能向外延伸"的方向逐步移动
   （高端 trim 只能向上、低端 trim 只能向下），迭代到无冲突为止，并复核全部已接受约束；
   若仍有冲突（需要向反方向移动才能满足），退回域端点解。

日志按用户要求逐个含弧子图输出：层、`subgraph=第几个/总数`、节点数、弧数、状态
（`solved`/`partial`/`invalid_domain`）、移动节点数、该子图的附加线长。无弧子图不逐行
输出，只在层汇总里以 `arc_free` 计数。

ASAP7 Ibex `5_2_route.odb` 上的实测结果（附加线长 = 移动后新增的金属长度）：

| 层 | 子图 | 含弧 | solved | partial | unresolved 弧 | 移动节点 | 附加线长 (um) | LPA 耗时 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| M2 | 21298 | 15128 | 14930 | 198 | 239 | 71474 | 6595.0 | 0.6s |
| M3 | 34891 | 20320 | 20273 | 47 | 54 | 20995 | 1209.5 | 0.2s |
| M4 | 13055 | 5437 | 5436 | 1 | 1 | 4306 | 150.1 | 0.04s |
| M5 | 6384 | 2426 | 2426 | 0 | 0 | 2618 | 54.0 | 0.02s |
| M6 | 3863 | 1550 | 1549 | 1 | 4 | 1811 | 135.3 | 0.01s |
| M7 | 2473 | 358 | 358 | 0 | 0 | 82 | 4.8 | 0.002s |

几点说明：

- 附加线长偏大是这一步的性质：本模型里 trim 只能向外延伸，而 B 类弧在 gap 小于 `Dt + W`
  时只能选择合并，合并意味着把间隙用金属填满（每个被合并的线端最多延伸 `gap - W`）。
  论文的流程同样把"最小可行解"作为 LPA 输出、由 EMA 压榨长度（图 8 的 110 → 图 11 的 30）。
- 只有极少数子图被判 unresolved（M2 239 条弧，约 0.08%），它们要么是间隙太小、
  要么是两端都被自己的金属挡住无法满足弧的区间。
- M2 有约 1.5 万个含弧子图，逐子图日志会产生上万行输出；需要安静模式或只打印
  慢/失败子图的话可以再加开关。

## EMA（延长线压缩）

第三步在 LPA 之后运行，不改变已选区间，只移动节点组缩短延长线（论文 §4.2.2，计划 03）。
`Leo::runEma` 调用 `solveEma()`，逐个含弧子图处理：

1. **紧闭包**：对每个 pivot 试 `q = +1`、`-1` 两个方向，沿 slack 为 0 的约束把必须同向移动的
   节点收进闭包 M（正向带上约束的目的端，负向带上源端）。
2. **步长**：先受闭包内节点域限制，再受跨越闭包的边中"会变紧"的那些边的 slack 限制，取最小值。
3. **目标变化**：`delta_cost = q * t * Σ weight[v]`（高端 trim 权重 +1、低端 -1），只提交
   `t > 0 且 delta_cost < 0` 的移动，因此每次移动严格降低整数目标，不会等成本循环。
4. **迭代**：取收益最大的候选（平局按 pivot ID、方向），提交后重算 slack，直到无改进候选；
   每子图上限 1000 次移动，只用于中断（本设计从未触发）。

ASAP7 Ibex 结果（"LPA 后"是本阶段输入，"EMA 后"是输出）：

| 层 | improved | unchanged | 移动次数 | LPA 后 (um) | EMA 后 (um) | 缩短比例 | EMA 耗时 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M2 | 5501 | 9627 | 33044 | 6595.0 | 1621.1 | 75.4% | 0.97 s |
| M3 | 1881 | 18439 | 5240 | 1209.5 | 484.7 | 59.9% | 0.15 s |
| M4 | 325 | 5112 | 490 | 150.1 | 90.8 | 39.5% | 0.02 s |
| M5 | 145 | 2281 | 186 | 54.0 | 36.0 | 33.3% | 0.006 s |
| M6 | 200 | 1350 | 323 | 135.3 | 76.4 | 43.5% | 0.006 s |
| M7 | 1 | 357 | 2 | 4.8 | 3.9 | 19.3% | 0.001 s |
| 合计 | 8053 | 37166 | 39285 | 8148.7 | 2312.9 | 71.6% | 1.14 s |

缩短比例与论文图例一致（论文从仅 B 弧解 110 到最终 30 缩短 72.7%）。`unchanged` 的子图多数是
已合并的一对 trim：金属只在两端之间转移、总长不变，因此没有任何改进方向。整次运行 15.5 s：
建图 13.5 s + LPA 0.9 s + EMA 1.1 s。

## 约束弧（A/B/C）

弧在每层内部建立，节点变量 `x` 取 trim 中心沿优选方向的坐标，因此阈值直接比较
**中心差**（不再做边距换算）。`x_dst - x_src` 的合法区间：

| 类型 | 合法中心差 | 建立条件 |
| --- | --- | --- |
| A | `[D, +∞)` | 同轨、非面对面的一对节点，必须保持分离（同一金属段两端的长度下限即属此类） |
| B | `[0,0] ∪ [Dt, +∞)` | 同轨、面对面的一段间隙两侧的 trim，可合并或保持 `Dt` |
| C | `(-∞,-Du] ∪ [0,0] ∪ [Du, +∞)` | 相邻轨（中心差 ≤ 一个 pitch）的 trim，可对齐或保持 `Du` |

"面对面"由 trim 所属的金属端方向判定：按优选方向排序后，左侧节点位于自身金属的
高端、右侧节点位于自身金属的低端，即为隔着一道间隙相向的两个线端。区间端点内部
按两倍 DBU 存储（`ArcRange`），与中心坐标保持同一精度。

**候选范围用移动域判定。** 一对节点 `(i, j)` 的可达中心差是
`[low2[j] - up2[i], up2[j] - low2[i]]`；若这个区间已经全部落在合法区间并集内，
说明两端无论怎么移动都不会违反规则，不建弧；否则建弧。搜索窗口取
`规则距离 + 2 × move_limit`（同轨用 `max(D, Dt)`，邻轨用 `Du`），保证不漏掉可能
进入违规区间的节点对。由于剪枝域 `[Lp,Up]` 是 `[L,U]` 的子集，这里判为冗余的节点对
在剪枝之后仍然冗余，不会漏弧。

ASAP7 Ibex `5_2_route.odb` 上的实测结果：

| 层 | A | B | C | 合计 | buildGraph |
| --- | --- | --- | --- | --- | --- |
| M2 | 60812 | 57099 | 164657 | 282568 | 6.9s |
| M3 | 10392 | 27005 | 73285 | 110682 | 4.7s |
| M4 | 3641 | 3291 | 9665 | 16597 | 1.1s |
| M5 | 2738 | 982 | 2473 | 6193 | 0.5s |
| M6 | 1658 | 1129 | 2544 | 5331 | 0.4s |
| M7 | 0 | 141 | 377 | 518 | 0.2s |
| 合计 | 79241 | 89647 | 253001 | 421889 | 13.8s |

弧数比"只连当前违规对"的版本大约 4 倍，原因是 20W 的移动上限很远：两个同轨节点
即使相距 40W，只要各自能移动，就可能进入 `(0, Dt)` 的违规区间，因此都要建弧。C 类
最多，因为相邻轨的作用距离是 `Du + 2 × move_limit ≈ 43W`。后续按论文
`P = k·Dt`（k 默认 3）剪枝域后，可达区间变窄，弧数会明显下降；建图时间也随之下降
（当前 M2 建图的 6.9s 主要花在加宽后的候选扫描上）。

A 弧仍以同 net 的低端–高端节点为主（短金属段的最小长度约束），B 弧是间隙两侧
面对面的 trim，C 弧是相邻轨上可能互相靠近的 trim。

## 按层运行与计时

`Leo::run()` 每次先清空上次的 trim、Vnode、移动域和弧并初始化一次，再按 M2、M3、
M4、M5、M6、M7 的顺序逐层执行 `buildGraph(block, layer)`、`runLpa(layer)`、
`runEma(layer)`；每层内部顺序是 trim → Vnode → 移动域 → 弧。
层名白名单限定当前 ASAP7 LEO 范围；不单凭 `DIRECTION` 是否为水平/垂直选择层。
M1、M8、M9 和 Pad 不生成 trim、不运行这三个阶段。缺失或无有效线宽/方向的层会
报告并跳过；未加载设计时不运行任何阶段。

每层输出三阶段耗时以及该层 `total`，单位为毫秒。最后一行 `LEO total timing`
中的 `total` 是已处理各层耗时之和，`run` 是包含一次初始化、层查找和层间报告的
整次运行墙钟时间。GUI 更新发生在 `run()` 返回之后，不计入这些算法运行时间。
各层 trim、节点和弧在一次运行内汇总，因此 GUI 同时保留 M2–M7 的显示。图只在内存中
保留，不写入数据库，也不改变版图；重复执行 `leo` 不累积节点或弧。当前整次运行
（含建图）在 ASAP7 Ibex 上约 14 秒，其中 M2 层建图约 6.9 秒，主要花在加宽后的弧
候选扫描上。
