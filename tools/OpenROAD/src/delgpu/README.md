# delgpu：CPU/GPU 一致的 trim 约束图

从当前 OpenDB 读取 M2–M7，生成 trim、节点、移动域和 A/B/C 约束边。
trim 使用 `module/del` 的 CPU 矩形并集与暴露端面算法；GPU 计算移动域、连接边和迭代更新。
优化结果是 trim 覆盖层的位移，不写回 OpenDB 实际布线。当前实现连续迭代和冲突修复，
尚未接入 CPU LP/MIP 后处理。

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
./dev-openroad.sh optimize --backend cpu --gui 0 --iterations 50
```

默认使用 `nproc` 检测到的全部可用线程，`-- --jobs=8` 可覆盖。
`gui` 子命令只打开设计；`graph --gui 1` 会自动执行建图并显示。
优化默认参数为 `-max_iterations 200 -max_step 0.25 -jump_offset_nm 35`，CPU/GPU 相同。
可用 `--max-step`、`--jump-offset-nm` 覆盖；实际参数会在日志中打印。
默认数据库是当前 ORFS 的 `flow/results/asap7/ibex/base/5_2_route.odb`。

## Tcl 命令

```tcl
delgpu -gui 1 -verify 1 -max_iterations 0 ;# 只建图、校验、显示
delgpu -gui 0 -max_iterations 200        ;# GPU 建图和迭代，关闭显示
delgpu -backend cpu -gui 0 -max_iterations 50 ;# CPU 参考迭代
```

- `-gui`：省略时跟随当前 GUI 是否启用。显式 `1` 要求 GUI 已启动；`0` 跳过
  渲染器注册、显示图保留和刷新。如果已有覆盖层，仅进行必要的一次清理刷新。
- `-verify`：默认 `0`。设置 `1` 时以同一组 trim 节点运行 CPU R-tree/建边参考实现，
  逐项比较中心、移动域、blocked、边类型/方向/ID 和合法区间；差异会报错。
  性能实验应关闭此项，否则包含额外 CPU 工作。
- `-max_iterations`：默认 200；`0` 只建图。`graph` 启动脚本默认 0，`optimize` 默认 200。
- `-max_step`：默认 `0.25`，普通梯度更新的每轮最大步长（线宽倍数）；停滞修复不受此限制。
- `-jump_offset_nm`：默认 `35`。CPU 使用该值作为跨势垒偏移；GPU 优先做合法区间投影，
  当合法目标不可达时尝试用该偏移跨过 B/C 势垒。两种修复策略仍有区别，参数均按工艺 DBU 换算。
- `-frame_ms`：默认 100，GUI 中间快照的最小间隔；`0` 仅显示初态和最终快照。
  不逐轮复制坐标，不为刷新间隔额外 sleep；每个实际 GUI 更新只调用 `pause(1)` 处理事件。
- `-report`：可选 CSV，记录每层初态、每轮及最佳快照；默认不写文件。
- `-backend`：默认 `gpu`，也支持 `cpu`。CPU 后端不初始化 CUDA；`-verify 1` 只用于 GPU。

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
脚本通过 `OPENROAD_CUDA_PATH`/`CUDA_HOME` 或 `nvcc` 定位工具包。
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
静态图、移动域、合法区间、CSR 邻接表和边着色索引只上传一次。
每轮为边势能/导数 → CSR 节点梯度/度数缩放 → 投影候选 → 全层能量下降接受。
候选最多三种扰动、每种八次回溯；只读取标量摘要，不下载整层坐标。
约束权重每 25 轮按 10/20/40/80 增长，硬移动域始终强制满足。

与 CPU 不要求相同轨迹：每三轮对停滞边按颜色并行修复，同色边无共享端点；
将节点对投影到最近的可达合法区间，A/B/C 及自定义 ArcRange 都参与；
若合法目标不可达，则 B/C 可以使用 `jump_offset_nm` 的跨势垒候选。
颜色依次在同一 stream 中执行，避免共享节点写竞争。修复可改变整体能量，
因此另存冲突数最少的快照；冲突数相同时优先最大残差较小、再优先总位移较小。
最后恢复最佳快照，以独立主机计算检查域、blocked 节点及最终冲突统计。
此策略不能保证全局最优或所有约束都可满足。

冲突按残差超过 `1e-6 DBU` 计数，该阈值只滤除浮点噪声。
另报 `remaining_5nm`（残差达到 5nm），两者不能混用。
`-verify 1` 校验的是建图一致性，不要求迭代与 CPU 相同。

无 GUI 时每层只在最终检查下载一次完整坐标。GUI 更新时下载当前层快照，
更新 trim、节点和边端点，不重复构图或复制所有层的图。
统计中的 `coordinate_download_bytes` 只计完整坐标，不计小摘要。
`total_ms` 包含优化初始化、迭代、最佳状态恢复及最终主机检查，排除建图和读库。

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

最新默认参数结果见 [200 / 0.25W / 35nm 验证](doc/default_parameters_validation.md)。
