# delgpu：CPU/GPU 一致的 trim 约束图

从当前 OpenDB 读取 M2–M7，生成 trim、节点、移动域和 A/B/C 约束边。
trim 使用 `module/del` 的 CPU 矩形并集与暴露端面算法；GPU 计算移动域、连接边和迭代更新。
优化结果是 trim 覆盖层的位移，不写回 OpenDB 实际布线。GPU 默认在连续迭代后执行
整数化、单/双端点冲突修复和安全回缩，不调用 CPU LP/MIP。

## 在 ORFS 根目录使用

```bash
./dev-openroad.sh build                    # CUDA 模块及支持 GUI 的宿主
./dev-openroad.sh build-delgpu             # 修改模块后的增量编译
./dev-openroad.sh graph --gui 1 --verify 1  # 打开 GUI、建图并校验
./dev-openroad.sh graph --gui 0             # 无界面 GPU 实验
./dev-openroad.sh graph --gui 0 --backend cpu
./dev-openroad.sh graph --db /路径/design.odb --gui 1
./dev-openroad.sh optimize --gui 0 --iterations 200 --report /tmp/delgpu.csv
./dev-openroad.sh optimize --gui 1 --frame-ms 100
./dev-openroad.sh optimize --postprocess 0  # 对照实验：仅连续迭代
./dev-openroad.sh optimize --backend cpu --gui 0 --iterations 50
```

默认使用 `nproc` 检测到的全部可用线程，`-- --jobs=8` 可覆盖。
`gui` 子命令只打开设计；`graph --gui 1` 会自动执行建图并显示。
GPU 优化默认参数为 `-gui 0 -max_iterations 150 -max_step 0.25 -jump_offset_nm 35
-learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5`。
可用 `--max-step`、`--jump-offset-nm` 覆盖；实际参数会在日志中打印。
GPU 默认不进行步长减半回溯；`--max-backtracks 0|7|19` 分别为零次、七次、十九次
减半重试（每种尝试总共 1、8、20 个候选步长）。CPU 参考始终保留 20 个候选步长。
默认数据库是当前 ORFS 的 `flow/results/asap7/ibex/base/5_2_route.odb`。

## Tcl 命令

```tcl
delgpu -gui 1 -verify 1 -max_iterations 0 ;# 只建图、校验、显示
delgpu                                ;# 动态 150 轮 + GPU 后处理，无 GUI
delgpu -postprocess 0                  ;# 只保留连续迭代结果
delgpu -gui 0 -max_backtracks 19         ;# 保留 CPU 的完整回溯预算
delgpu -gui 0 -max_iterations 150 -learning_rate 1.5 -learning_rate_end 1.0
delgpu -gui 0 -max_iterations 150 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
delgpu -backend cpu -gui 0 -max_iterations 50 ;# CPU 参考迭代
```

- `-gui`：默认 `0`，即使在 GUI 控制台中也需显式 `1` 才显示。`1` 要求 GUI 已启动；`0` 跳过
  渲染器注册、显示图保留和刷新。如果已有覆盖层，仅进行必要的一次清理刷新。
- `-verify`：默认 `0`。设置 `1` 时以同一组 trim 节点运行 CPU R-tree/建边参考实现，
  逐项比较中心、移动域、blocked、边类型/方向/ID 和合法区间；差异会报错。
  性能实验应关闭此项，否则包含额外 CPU 工作。
- `-max_iterations`：默认 150；`0` 只建图。`graph` 启动脚本默认 0，`optimize` 默认 150。
- `-max_step`：默认 `0.25`，普通梯度更新的每轮最大步长（线宽倍数）；停滞修复不受此限制。
- `-jump_offset_nm`：默认 `35`。CPU/GPU 均使用 `midpoint ± offset` 跨过 B/C 势垒，
  偏移按工艺 DBU 和层线宽换算。
- `-max_backtracks`：仅 GPU，默认 `0`，不进行步长减半重试；范围 `0..19`。
  保留全层能量下降检查，初始方向失败后仍可尝试 CPU 的两种确定性扰动。
  `19` 对应 CPU 每种尝试最多 20 个候选步长；CPU 后端不接受该选项。
- `-learning_rate`：仅 GPU，默认 `1.5`，独立于规则权重 lambda 的移动比例。
  普通梯度步长和上限一起缩放：有效上限为 `max_step × learning_rate × W`，
  最后仍强制裁剪到移动域；能量下降检查和 CPU 跳跃规则继续生效。
- `-learning_rate_end`：末轮比例，当前默认动态模式下为 `0.5`。`linear` 模式下省略时与初值相同，为固定学习率；
  设置后在本次预算内逐轮线性插值。`adaptive` 模式将它用作下限，省略时为
  `min(初值, 0.5)`。单轮预算只使用初值。
  学习率均要求有限正值，且与 `max_step` 的乘积必须有限且大于零。
  它们只缩放普通梯度更新，不缩放停滞跳跃的 35nm 偏移。
  启动脚本对应 `--learning-rate` 和 `--learning-rate-end`。
- `-learning_rate_mode`：仅 GPU，默认 `adaptive`，也可设 `linear`。
  动态模式每层独立使用余弦递减上限；连续两轮梯度/扰动阶段没有接受候选时乘 0.8，
  八轮内规则能量和 ≥5nm 数量均停滞时乘 0.85，直到达到下限。
  比例只减不增，末轮达到下限；下限必须不超过初值。
  直接使用已有统计，不增加候选或坐标下载。启动选项为 `--learning-rate-mode`。
- `-postprocess`：GPU 默认 `1`，CPU 默认 `0`；CPU 不支持开启此 GPU 后处理。
  `0` 保留连续末态；`-max_iterations 0` 始终只建图。
- `-post_sweeps`：修复扫描上限，默认 `24`。每轮先扫描单端点，停滞后尝试一轮双端点；
  提前无改善即停止。`0` 跳过单/双端点修复，可搭配回缩预算进行消融实验。
- `-post_pair_rounds`：双端点搜索轮数上限，默认 `8`；`0` 禁用双端点搜索。
- `-post_retract_mode`：默认 `cooperative`，在原单点回缩停滞后追加整组协同回缩和
  合并组平移；`single` 可复现原方法。允许个别端点延长，但总延长量必须下降，
  每条关联边残差均不能增加。启动选项为 `--post-retract-mode`。
- `-post_retract_sweeps`：安全回缩扫描上限，默认 `8`；`0` 禁用回缩。
  三项均接受非负整数；启动脚本对应 `--post-sweeps`、`--post-pair-rounds`、
  `--post-retract-sweeps`，后处理开关对应 `--postprocess`。
- `-frame_ms`：默认 100，GUI 中间快照的最小间隔；`0` 仅显示初态和最终快照。
  不逐轮复制坐标，不为刷新间隔额外 sleep；每个实际 GUI 更新只调用 `pause(1)` 处理事件。
- `-report`：可选 CSV，记录每层初态、每轮及末态（`final`）；默认不写文件。
  `continuous` 保存连续末态；后处理开启时增加 `quantized`、`repaired`、`retracted` 三阶段，
  `final` 和最终显示采用后处理结果，不恢复历史快照。
  `violations_lt5nm` 记录严格 `0 < 残差 < 5nm` 的数量，等于 `conflicts - remaining_5nm`；
  恰好 5nm 计入 `remaining_5nm`，残差为 0 不计入违例。
  `sum_gap_dbu` 记录整数阶段的残差总量，连续阶段留空；`stage_ms` 为后处理各阶段耗时。
  整数阶段不使用连续势能，CSV 中 `energy`/`rule_energy` 为 0。
  `learning_rate` 和 `effective_max_step_w` 列记录当轮实际学习率及梯度步长上限。
  动态诊断列 `lr_feedback_reductions` 记录反馈降速次数，`rule_energy` 为不乘 lambda
  的规则能量；`gradient_accepted` 在 GPU 后端区分梯度接受和仅成功跳跃。
- `-backend`：默认 `gpu`，也支持 `cpu`。CPU 后端仍使用原算法、单位学习率和完整回溯，
  迭代预算默认 150，可显式指定 200；不初始化 CUDA。`-verify 1` 只用于 GPU。

显示控制位于 `DELGPU/<层>`：`trims`、`vnodes`、`arcs/A`、`arcs/B`、`arcs/C`，
以及默认关闭的 `domains`。trim/节点为黄色，A/B/C 边分别为红/绿/青。
重复运行会替换旧覆盖层；数据库销毁后通过 OpenDB 回调所有权检查避免访问旧 block。

## GPU 实现和精确性

CPU 读取普通 dbWire 和 via 金属盒，按网络、层求矩形并集，再生成 W×2W trim。
供电网络只提供障碍物；障碍集合与 `module/del` 一致，不额外加入 special wires/pin/blockage。
节点的全局 trim 序号、层内排序和 ID 保持 DEL 的规则。

CPU 为金属矩形准备平衡 BVH，为节点准备按横轨/沿轨坐标排序的轨道索引。
GPU 每节点遍历 BVH、查询最大移动范围内的障碍物，严格区分接触和内部重叠。
随后仅搜索同轨和横距不超过 pitch 的邻轨，利用二分查找限制沿轨候选窗口。
两遍核分别计数和写边，中间用 CUB 前缀和分配位置，避免 N×N 节点对矩阵。
最后在 CPU 按 type/src/dst 排序，分配与参考实现一致的边 ID 和合法区间。
全程几何判断使用 int64 双倍 DBU，不使用浮点容差。

`domains_arcs_gpu` 是包含 CPU 索引准备、上传、GPU 运算、下载、边结构恢复和排序的
端到端阶段时间，不能当作纯设备核耗时。`geometry_cpu` 单列 trim 和节点生成。
`build_ms` 包含建图和可选校验，排除数据库加载、CUDA 初始化及 GUI 绘图；
`display_ms` 单列显示数据注册时间，不包含之后的 Qt 重绘。
只建图且关闭 GUI 时逐层释放图。迭代时保留主机图供初始化使用，每次只为一层申请设备状态；
GPU 缓冲通过 RAII 在每层结束/异常时释放。

## 工具链和验证

Bazel 使用仓库的 Clang CUDA 编译能力和 `@cuda_local`，无需 Kokkos/KokkosFFT。
CUDA 工具包需包含 Runtime、CUB/CCCL、libdevice、ptxas/fatbinary 和 cuRAND 头文件
（Clang 的 CUDA runtime wrapper 会包含 cuRAND 头）。默认嵌入 sm_75 SASS 和 PTX，
新设备可由驱动 JIT；首次运行的 JIT 成本应与预热后性能区分。
脚本依次检查 `OPENROAD_CUDA_PATH`、`CUDA_HOME`、`CUDA_PATH`、PATH 中的 `nvcc`，
以及项目、ORFS、工作区的 `.tools/cuda*`、`dependencies/cuda*`、用户的
`~/.local/opt/cuda*` 和 `/usr/local`、`/opt` 下的常见 CUDA 安装目录。
无需将 `nvcc` 加入 PATH；会校验头文件、ptxas/fatbinary、libdevice 和 libcudart。
显式指定的 CUDA 路径无效时直接报错，不静默改用另一个版本。
无 CUDA 构建使用 `--cpu`，运行时选择 `-backend cpu`。
CMake 配置也保留 CPU 和 CUDA 源文件及测试注册。

```bash
# 在 tools/OpenROAD 目录执行
OPENROAD_CUDA_PATH=/实际/CUDA/路径 bazelisk test \
  --//:platform=gui --//src/delgpu:gpu=cuda \
  --test_output=errors //src/delgpu:graph_gpu_test

# 在 ORFS 根目录执行，使用 offscreen Qt 自动检查显示和关闭显示
QT_QPA_PLATFORM=offscreen ./dev-openroad.sh run -gui -no_settings -exit \
  tools/OpenROAD/src/delgpu/test/gui_graph.tcl
```

`gui_graph.tcl` 和 `gui_optimizer.tcl` 依赖上述 ibex 数据库及固定观察窗口；
更换数据库时需调整截图窗口。`DELGPU_IMAGE_DIR` 指定图像输出目录。
加载脚本也支持 `DELGPU_GUI`、`DELGPU_VERIFY`、`DELGPU_BACKEND`。
验证数据与性能记录见 [建图验证记录](doc/graph_validation.md)。

## GPU 迭代实现

`src/optimize/TrimOptimizer.cu` 实现常驻坐标/图/工作缓冲、单非阻塞 stream、CUB 归约。
静态图、移动域、合法区间、CSR 邻接表和连通分量索引只上传一次。
每轮为边势能/导数 → CSR 节点梯度/度数缩放 → 投影候选 → 全层能量下降接受。
候选为普通方向加两种确定性扰动，默认每种只试初始步长；只读取标量摘要，不下载整层坐标。
约束权重每 25 轮按 10/20/40/80 增长，硬移动域始终强制满足。

停滞资格、修复目标和计数清零规则与 CPU 一致：旧、新残差均 ≥5nm，
端点连续三轮无可见移动时，每轮检查 B/C 跨势垒跳跃。成功后立即清零
实际移动端点的关联边。A 和负距离 B 不进行跨势垒跳跃。
每个 GPU 线程拥有一个独立连通分量，分量内按原边顺序修复；分量之间无共享节点，
可以并行。移动域仍为硬边界，跳跃不受普通梯度最大步长和能量下降条件限制。
默认接入下述 GPU 后处理，再下载末态，以独立主机计算检查域、blocked 节点及最终统计。
默认取消步长减半会改变梯度轨迹；完整回溯模式仍可能因浮点归约顺序出现临界分支差异。
该方法不能保证全局最优或所有约束都可满足。

连续阶段的冲突按残差超过 `1e-6 DBU` 计数，该阈值只滤除浮点噪声。
整数后处理使用原始双倍 DBU 精确残差，严格以 `gap2 > 0` 判断违例，不使用浮点容差。
另报 `remaining_5nm`（残差严格按 ≥5nm 判断，不添加数值容差），两者不能混用。
`-verify 1` 校验的是建图一致性，不要求迭代与 CPU 相同。

无 GUI 时每层只在最终检查下载一次完整坐标。GUI 更新时下载当前层快照，
更新 trim、节点和边端点，不重复构图或复制所有层的图。
统计中的 `coordinate_download_bytes` 只计完整坐标，不计小摘要。
`total_ms` 包含优化初始化、迭代、后处理、最终下载及主机检查，排除建图、读库和 CUDA 上下文初始化。
`iteration_ms` 是各层循环耗时之和；GUI 开启时包含绘图事件处理，实验应关闭 GUI。
`extent_um` 为末态绝对 trim 位移之和；`wire_extension_um` 按高/低端方向求净延长量。
当前数据库的移动域只允许向外移动，二者相同。它们描述 trim 模型，不表示已修改实际布线。

GUI 回归可运行 `test/gui_optimizer.tcl`（同样使用 offscreen Qt）；
该测试检查迭代后图像变化，以及 `-gui 0` 清除覆盖层。
当前层次化 ibex 的 GUI 数据库销毁/重载存在宿主问题，切换设计请重启 GUI；
headless 数据库重载已通过单独回归。

结果和性能见 [迭代验证记录](doc/iteration_validation.md)。

默认参数的预热后三次交替 CPU/GPU 测试：

```bash
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_defaults.tcl
```

该脚本直接调用省略算法参数的 `delgpu`，以验证代码默认值；GUI/建图校验关闭，
每次输出独立 CSV。`DELGPU_BENCHMARK_DIR` 可指定输出目录。
历史验证记录中的 0.2W/5nm 实验属于旧参数；新默认的记录单独列出。

历史投影修复结果见 [200 / 0.25W / 35nm 验证](doc/default_parameters_validation.md)。
当前 CPU 跳跃规则和无回溯默认值的结果见 [跳跃与回溯实验](doc/cpu_jump_validation.md)。

```bash
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_cpu_jump.tcl
python3 tools/OpenROAD/src/delgpu/scripts/summarize_cpu_jump.py
```

该实验预热 GPU 后交替测量 CPU、GPU 零次/七次/十九次减半重试，各三次。
`DELGPU_BENCHMARK_DIR` 可覆盖输出目录，汇总脚本可接收同一个目录参数。

学习率扫描及缩短预算的结果见 [学习率实验](doc/learning_rate_validation.md)。
线性调度实验中，150 轮 1.5→1 得到 85 条 ≥5nm 违例。
新的 [动态学习率实验](doc/adaptive_rate_validation.md) 中，150 轮 1.5→0.5 得到
64 条（仅连续阶段，使用 `-postprocess 0` 复现），优化耗时中位数 1.917 秒，比固定 200 轮节省约 27.8%。
这组动态 150 轮参数现已设为代码、Tcl 和 `optimize` 启动命令的默认值。
不带参数的 `delgpu` 或 `./dev-openroad.sh optimize` 即可运行，显示需显式开启。


## GPU 整数后处理

通俗图解见 [后处理计划](doc/gpu_postprocess_plan.md)，实测见
[后处理验证](doc/postprocess_validation.md)，新版回缩对照见
[安全回缩优化](doc/retraction_validation.md)。主机只准备原始整数几何和确定性节点分组，
复用现有 GPU CSR/连通分量，位移继续留在设备上，不重新建 trim 图。

处理顺序为：域内整数位移 → 分颜色单端点搜索 → 停滞时双端点搜索 → 安全回缩。
修复按 `(≥5nm 数量, 所有正残差数量, 残差总量, 绝对位移总量)` 顺序接受严格改善，
检查移动端点关联的全部规则；双端点按关联边并集评分，每个连通分量每轮仅提交一个方案。
回缩要求每条关联边残差不增加且绝对位移总量下降。
默认在单点回缩停滞后执行整数差分界传播，允许一组端点同时向原点回退；
再对合并组求最优整体平移，参考 DEL 的 LP 允许部分端点伸长、总体缩短的做法。
合并组每个连通分量每次只提交一个方案，所有方案先逐边验证再写坐标。
GPU 分组和候选全过程留在设备上；传播上限内无法通过检查的分量保留原位置。
最终由独立 CPU 整数检查器
直接检查原始 Vnode/ArcRange；它不调用 LP/MIP，也不重新运行连续优化。

后处理属于局部搜索，不保证全局最优。整数化可能新增违例，单调改进保证从
`quantized` 开始成立。GUI 开关不影响算法；关闭 GUI 时无中间完整坐标下载或绘图，
最终每层仅下载一次整数坐标。`postprocess_ms` 包含主机准备、分组、上传、设备缓冲
申请/释放、整数化、修复和回缩；最终独立主机检查计入优化 `total_ms`。
`device_mib` 是该层的常驻优化缓冲大小，不代表后处理临时缓冲的峰值。

```bash
# 在 ORFS 根目录：复现初版单点回缩，交替比较连续、仅取整、修复、完整后处理，各 3 次
mkdir -p flow/reports/asap7/ibex/base/delgpu_postprocess
./dev-openroad.sh run -no_splash -exit \
  tools/OpenROAD/src/delgpu/scripts/benchmark_postprocess.tcl \
  > flow/reports/asap7/ibex/base/delgpu_postprocess/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_postprocess.py \
  flow/reports/asap7/ibex/base/delgpu_postprocess
```

历史学习率、跳跃/回溯实验脚本已显式关闭后处理，以保持其原有对照含义。

新版安全回缩默认 8 次扫描，在同一修复末态上将延长量从 2632.331µm 降到
2607.572µm，≥5nm 与 <5nm 违例仍为 36/48。预热后三次测试中，回缩阶段耗时中位数
从 6.344ms 增至 23.955ms。16 次得到 2607.525µm，收益仅 0.047µm，故保留默认 8 次。

```bash
# 比较原方法及新版 8/16/32 次，三次交替测量，保持修复输入完全一致
mkdir -p flow/reports/asap7/ibex/base/delgpu_retraction
./dev-openroad.sh run -no_splash -exit \
  tools/OpenROAD/src/delgpu/scripts/benchmark_retraction.tcl \
  > flow/reports/asap7/ibex/base/delgpu_retraction/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_retraction.py \
  flow/reports/asap7/ibex/base/delgpu_retraction
```
