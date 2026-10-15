# LEO（Line End Optimizer）

当前按论文 §4.1、§4.2 的流程实现剪枝、LPA 和 EMA，删除此前额外的坐标修复、
全局最大收益选择和求解状态复用。当前 ibex 验证结果及实现细节见
[论文流程基准](doc/paper_baseline_validation.md)。
[此前审计](doc/algorithm_audit.md)和[旧说明存档](doc/pre_paper_policy_readme.md)记录修改前的版本。

## 构建与运行

在 **OpenROAD-flow-scripts 根目录**执行：

```bash
./dev-openroad.sh build
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/leo/scripts/benchmark_baseline.tcl
```

未指定 `--jobs` 时编译使用当前全部线程；也可 `build -- --jobs=8`。
当前已验证的是 Bazel GUI/CUDA 配置。LEO 静态链接进 OpenROAD，改动后需重新构建整个
OpenROAD 可执行文件并启动新进程。模块也维护 CMake 源文件和测试注册，但本次未运行 CMake。

加载详细布线后的 ODB，在 Tcl 控制台调用：

```tcl
leo
# 与默认调用相同：k=3，P=3*Dt
leo -prune_factor 3 -report /tmp/leo.csv
```

`-prune_factor` 必须为正整数。没有启用 OpenROAD GUI 时不会创建显示数据；
在 GUI 中执行相同命令会更新 `LEO/<layer>/trims`、`vnodes`、`arcs/A|B|C`、`moves` 覆盖层。
当前仍只移动图中 trim，不写回 OpenDB 的 routed wire。

## 当前算法

1. 从 dbWire 及 via 金属形状生成 trim，建立节点并计算金属障碍限定的完整移动域。
2. 按 `P=k*Dt` 剪枝：正权重保留 `[L,min(U,L+P)]`，负权重保留 `[max(L,U-P),U]`。
   原 20W 上限已移除，`k>4` 也能使用超过 20W 的域。
3. 删除在整个剪枝域内都合法的冗余弧，划分弱连通分量。
4. LPA 按弧类型、初始 slack、稳定 ID 排序，优先尝试靠近 ideal 的区间。
   每次试探将全部已接受约束和候选约束从头进行最长路求解，选第一个可行区间，
   不撤销之前已接受的弧；失败弧保留用于最终统计。
   最终坐标直接取 oracle 的最小可行向量，**没有 16 遍 ideal 坐标修复或修复失败回退**。
5. EMA 按固定节点 ID 顺序选择 pivot，只沿该节点减少延长量的方向形成零 slack 紧约束树，
   由边界 slack 和域限制步长，遇到第一个负代价移动立即提交，再从头扫描。
   没有全局最大收益排名、双向候选比较或 1000 次人工移动上限。
6. LPA 每次从域边界重新计算上下界；EMA 每次重新扫描约束建立树和计算 slack。
   不保留跨试探的传播队列、上下界、事务回滚状态，也不复用跨子图工作区或 EMA 邻接/闭包缓存。

论文没有规定 EMA 的 pivot 顺序、方向选择与平局规则；上面的固定顺序及单方向是明确公开的
实现约定，不能宣称与原作者未公开程序逐步一致。目前仍为 A/B/C 简化工艺模型，
`D=5W, Dt=5W, Du=3W`，未实现完整 EUV D/E 规则和 rLEO 拆线重布。

## 统计与复现实验

全部违例按弧到**所有合法区间并集**的最小残差 `>0` 统计，使用双倍 DBU 整数坐标，
没有 5nm 过滤或数值 epsilon。未接受的弧同样计入，`remaining_5nm` 仅为辅助指标。
CSV 还输出 A/B/C 分类、已选区间违例、图中模型延长量和最大残差。
这属于图约束评估，没有执行最终物理 DRC。

基准脚本预热一次、计时三次，计时包含建图、求解、报告和 CSV，不含读取 ODB。
实验应将日志和报告先写入 Linux `/tmp`，计时后再复制到 `/mnt/e`，避免跨文件系统 I/O 干扰：

```bash
mkdir -p /tmp/leo-paper-run
LEO_PRUNE_FACTOR=3 LEO_BENCHMARK_DIR=/tmp/leo-paper-run \
  ./dev-openroad.sh run -no_splash -exit \
  tools/OpenROAD/src/leo/scripts/benchmark_baseline.tcl \
  > /tmp/leo-paper-run/benchmark.log 2>&1
```

可用 `LEO_ROUTE_DB` 指定 ODB。阶段审计 `leo::audit_stages_cmd /tmp/stages.csv`
在复制图上重放 initial/LPA/EMA，不修改当前显示结果。

## 代码与验证

- `src/graph/Domains.cpp`：完整障碍域及论文剪枝；`Arcs.cpp`：冗余约束剔除。
- `src/lpa/DifferenceConstraints.{h,cpp}`：每次独立进行完整 Bellman–Ford 扫描的 oracle。
- `src/lpa/Lpa.cpp`：区间贪心选择及直接取可行坐标。
- `src/ema/TightTree.cpp`、`Ema.cpp`：重新构建紧约束树及固定顺序首个改进。
- `src/ComponentModel.h`：每阶段独立创建当前子图数据，无跨子图缓存。
- `src/Measurement.cpp`：全部违例与延长量评估。

测试均在 CMake/Bazel 注册；当前验证命令（在 `tools/OpenROAD` 目录执行）：

```bash
OPENROAD_CUDA_PATH=/home/karl/.local/opt/cuda-12.9.1 bazelisk test --jobs=8 \
  --//:platform=gui --//src/delgpu:gpu=cuda --test_output=errors //src/leo/test:all
```
