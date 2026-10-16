# DEL / LEO / DELGPU 全部实验方法、命令与结果汇总

整理日期：2026-10-16（Asia/Shanghai）。实验归档分支：`tmp-gpu-ema`；初始备份提交：`fc0016e6eb4da26641c0dd51764ed04ccf2a1e7b`。后续功能与本文随该分支的归档提交一并保存，复现应使用分支最新归档版本。

本文主体整理归档时已有的实验；最终配置与后续整数迭代撤回记录另见下文及最终方案验证。包括建图、CPU/GPU连续迭代、修复语义诊断、回溯、固定/线性/动态学习率、整数后处理、协同回缩、GPU联合网络搜索、EMA、线长权重、缩短轮次、仅后处理，以及LEO/原版DEL对照。单次探索与三次确认分别列出；缺失项标为“—”，不补造测量值。

## 0. 使用本文件前先明确的口径

- 同一主输入：`flow/results/asap7/ibex/base/5_2_route.odb`，ASAP7 / IBEX，M2–M7，DBU/µm=1000。
- 输入SHA256：`381e1ddc86e2e9f1017fcd1094d11b969efa8fee40d32c50ff6d0f082ab4d744`。
- 普通布线原长：89,786.472µm。20W图为332,664节点、437,360边；LEO的15W剪枝后为372,205边。
- “增加线长”指trim模型的绝对位移总和；本输入移动域向外，它与带方向净延长量相等。各实验未写回实际dbWire，并核对原布线长度不变。
- “DRC/冲突/违例”在本文指简化A/B/C图约束残差，不是完整物理DRC。≥5nm包含恰好5nm；小违例严格为0<残差<5nm。
- 连续阶段过滤≤1e-6 DBU的数值噪声；整数后处理按原始双倍DBU几何精确计数。取整本身会改变数量。
- **“EMA 100轮”是100轮连续迭代加整数修复和EMA；“仅100轮”是关闭后处理的连续末态。两者不得混用。**
- `wall_ms`/“命令时间”含建图、优化、报告与末态检查，通常不含read_db和预热；`total_ms`/“优化时间”通常不含建图。每张表单独注明。
- 三次重复的时间取各列独立中位数，不删除慢样本；单次扫描时间只作探索，不与中位数拼接比较。不同批次同一方法的时间会波动。
- 实验GPU：RTX 5060 Laptop 8GiB，CUDA Toolkit 12.9.1；早期记录的CUDA Runtime为12.9。性能实验GUI/verify关闭，日志和CSV优先写Linux `/tmp` 后归档。

当前 `main` 最终默认值：100轮、0.25W、35nm、adaptive学习率1.5→0.5、线长权重0.1、零次步长减半、整数修复24/双点8/EMA预算8、ema_lpa、joint=0、GUI=0。LEO默认k=4，即20W。

2026-10-16 已撤回“浮点100轮后追加整数迭代”的实验入口，恢复已验证的 EMA100 方案。整数100+50的结果为2581.231µm、≥5nm 58条、小违例1311条，未达到EMA100的2453.232µm、36/44条，因此不作为最终方案。实验源码与原始数据保留在 `flow/reports/asap7/ibex/base/delgpu_integer_iteration/`，源码归档为 `rollback_snapshot/integer_iteration_source.tar.gz`。最终默认命令与复测见 [EMA100最终验证](tools/OpenROAD/src/delgpu/doc/final_ema100_validation.md)。

`tmp-gpu-ema` / `fc0016e` 是EMA/联合搜索初始备份。其后的线长权重、LEO默认20W、仅后处理入口等增量随本次归档提交保存；只checkout初始备份提交不等于拿到全部最新功能。main上单独调权重的原修改另保存在stash `c9c34089007fd8c080ed026346b4dd448cf00beb`。

### 主要证据批次

| 批次 | 内容 | 证据位置 | 统计说明 |
| --- | --- | --- | --- |
| G | 建图，2026-10-14 | tools/OpenROAD/src/delgpu/doc/graph_validation.md | CPU/GPU各5次；只计建图 |
| H | 早期投影/最佳快照，2026-10-14 | tools/OpenROAD/src/delgpu/doc/iteration_validation.md | 历史版本；不代表当前算法 |
| C | CPU跳跃与回溯，2026-10-15 | flow/reports/asap7/ibex/base/delgpu_cpu_jump_200/summary.json | 三次；仅连续末态 |
| LR | 固定/线性学习率 | flow/reports/asap7/ibex/base/delgpu_learning_rate/summary.json | 筛选+三次确认 |
| AD | 动态学习率 | flow/reports/asap7/ibex/base/delgpu_adaptive_rate/summary.json | 筛选+三次确认 |
| P | 初版整数后处理 | flow/reports/asap7/ibex/base/delgpu_postprocess/summary.json | 三次；single回缩 |
| R | 协同回缩 | flow/reports/asap7/ibex/base/delgpu_retraction/summary.json | 三次；同一修复末态 |
| T | 三方法最终质量 | flow/reports/asap7/ibex/base/delgpu_three_methods/comparison.json | 每种单次；含DEL LP/MIP |
| J | 联合网络搜索 | flow/reports/asap7/ibex/base/delgpu_joint/summary.json | 四组各三次，另有单次预算扫描 |
| E | EMA模式 | flow/reports/asap7/ibex/base/delgpu_ema/summary.json | 八组各三次；LEO当时为15W |
| W | 单独线长权重 | flow/reports/asap7/ibex/base/delgpu_wire_weight/summary.json | 五组各三次+17次单次探索 |
| EW | EMA叠加权重 | flow/reports/asap7/ibex/base/delgpu_ema_weight/confirm/summary.json | 三次确认+46组EMA单次探索 |
| N | 权重0.1缩短轮次 | flow/reports/asap7/ibex/base/delgpu_ema_iterations/confirm/summary.json | 19个预算探索，8个预算三次确认 |
| Z | 仅后处理最终对照 | flow/reports/asap7/ibex/base/delgpu_postprocess_only/confirm/summary.json | 最新同批次100/50/0轮+LEO三次确认 |

## 1. 主要方法快速对照（最终后处理结果）

下表用于定位方法，**跨批次时间不是严格同场排名**。T为单次，其他时间为相应批次三次命令中位数。完整参数和全部探索见后续章节。

| 方法 | 增加线长 µm | ≥5nm | 0<残差<5nm | 命令时间 s | 批次 |
| --- | --- | --- | --- | --- | --- |
| DEL200 + LP/MIP | 2423.544 | 36 | 40 | 76.284 | T，单次 |
| LEO15W LPA+EMA | 2441.818 | 336 | 4 | 2.936 | E |
| LEO20W（当前默认） | 2441.843 | 335 | 5 | 2.997 | Z |
| GPU150 cooperative | 2607.572 | 36 | 48 | 2.546 | Z |
| GPU150 联合网络1×2 | 2523.814 | 36 | 48 | 2.997 | J |
| GPU150 联合搜索1×16 | 2456.274 | 36 | 48 | 3.773 | J |
| GPU150 联合搜索1×64 | 2433.808 | 36 | 48 | 5.422 | J |
| GPU150 纯EMA | 2523.749 | 36 | 48 | 2.607 | E |
| GPU150 EMA区间跳跃 | 2519.160 | 36 | 48 | 2.599 | E |
| GPU150 ema_lpa，α=.01 | 2452.195 | 36 | 48 | 2.816 | E |
| GPU100 ema_lpa，α=.01 | 2453.705 | 36 | 44 | 2.138 | E |
| GPU100 ema_lpa，α=.1 | 2453.232 | 36 | 44 | 2.062 | Z |
| GPU75 ema_lpa，α=.1 | 2456.713 | 36 | 48 | 1.776 | N |
| GPU50 ema_lpa，α=.1 | 2464.489 | 36 | 48 | 1.470 | Z |
| GPU26 ema_lpa，α=.1 | 2477.916 | 36 | 40 | 1.228 | N |
| GPU仅后处理，ema_lpa | 2478.065 | 44 | 36 | 0.985 | Z |

主要取舍：联合搜索的线长最接近DEL，但时间显著增加；EMA候选能兼顾时间和线长；单独/叠加线长权重收益小；50轮比100轮更快但长度与残差略增；零轮最快，严重违例增加。当前没有证明36条≥5nm是不可突破的下界，也没有证明GPU达到全局最优。

## 2. 自行编译、启动、建图与GUI

```bash
cd /mnt/e/202610/code/OpenROAD-flow-scripts
./dev-openroad.sh build                     # CUDA模块 + GUI宿主；默认nproc全部线程
./dev-openroad.sh build -- --jobs=8          # 显式限8线程
./dev-openroad.sh build-delgpu              # 仅模块；首次需要先build
./dev-openroad.sh build-delgpu --cpu         # CPU-only模块；GPU实验前需重新build恢复CUDA
./dev-openroad.sh gui                       # 自动读默认IBEX数据库
./dev-openroad.sh gui --db /path/design.odb
./dev-openroad.sh run                       # Tcl终端
./dev-openroad.sh graph --gui 0 --backend gpu
./dev-openroad.sh graph --gui 0 --backend cpu
./dev-openroad.sh graph --gui 0 --verify 1   # CPU/GPU建图逐项核对
./dev-openroad.sh graph --gui 1             # 显示trim/节点/边
```

在GUI Tcl Console中：

```tcl
delgpu -gui 1 -max_iterations 0  ;# 显示初始图
delgpu -gui 0 -max_iterations 0  ;# 清除显示/仅建图
```

| 后端 | 建图中位数 ms | 样本数 | 结果 |
| --- | --- | --- | --- |
| CPU | 719.708 | 5 | 332664节点/437360边 |
| GPU | 447.944 | 5 | 逐项匹配CPU；约1.61倍建图加速 |

来源：[详细记录](tools/OpenROAD/src/delgpu/doc/graph_validation.md)。
GUI开关控制显示和中间坐标快照，性能实验使用 `-gui 0`。无GUI时全设计最终坐标下载2,661,312bytes，每层一次；不能把建图的1.61倍加速直接当作整个优化流程加速。

## 3. 后续各方法命令共用的明确参数模板

下面的 `run_gpu` 是**本文定义的Tcl辅助过程**，须先执行本代码块，再执行后面的具体方法命令。它显式固定所有关键默认值，避免版本和默认参数变化影响复现。将本代码块与选中的方法命令保存为 `/tmp/reproduce_delgpu.tcl` 后，用下方shell命令执行即可。

```tcl
read_db /mnt/e/202610/code/OpenROAD-flow-scripts/flow/results/asap7/ibex/base/5_2_route.odb
set ::summary_report_dir /tmp/delgpu-all-methods
file mkdir $::summary_report_dir
proc run_gpu {name args} {
  set options [dict create \
    -gui 0 -verify 0 -backend gpu -max_iterations 150 \
    -max_step 0.25 -jump_offset_nm 35 -max_backtracks 0 \
    -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5 \
    -wire_weight 0.01 -postprocess 1 -postprocess_only 0 \
    -post_sweeps 24 -post_pair_rounds 8 -post_retract_sweeps 8 \
    -post_retract_mode cooperative -post_joint_rounds 0 -post_joint_trials 16]
  foreach {key value} $args {dict set options $key $value}
  dict set options -report [file join $::summary_report_dir ${name}.csv]
  delgpu {*}$options
}
```

```bash
./dev-openroad.sh run -no_splash -exit /tmp/reproduce_delgpu.tcl
```

本文表后的 `run_gpu ...` 都是该模板上的完整覆盖参数。CPU参考、原版DEL、LEO命令直接调用各自入口。旧投影和临时诊断版本另行标记，不用当前模板冒充历史实现。

## 4. CPU参考、原版DEL与LEO的运行命令

```tcl
# 原DEL 200轮 + 原生LP/MIP后处理，分步入口需要c继续执行到结束
del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35 \
  -report /tmp/delgpu-all-methods/native_del200.csv
c

# delgpu内的CPU连续参考；不含原DEL LP/MIP
delgpu -gui 0 -backend cpu -max_iterations 200 -max_step 0.25 \
  -jump_offset_nm 35 -postprocess 0 -report /tmp/delgpu-all-methods/cpu200.csv

# LEO：15W历史对照和20W当前默认
leo -prune_factor 3 -report /tmp/delgpu-all-methods/leo15.csv
leo -prune_factor 4 -report /tmp/delgpu-all-methods/leo20.csv
leo  ;# 当前等价于-prune_factor 4
```

原DEL第一次原命令运行（未开report）的连续第200轮：83142条总违例、90条≥5nm；原生LP/MIP后为76条总违例、36条≥5nm、40条小违例。该单次整个调用含setup约76.565秒。随后T批次开启CSV重新测得76.284秒，最终增加量2423.544µm。两次均为单次，不用来宣称稳定性能加速比。

| LEO配置 | 剪枝域 | 保留边数 | 增加量 µm | ≥5nm | 小违例 |
| --- | --- | --- | --- | --- | --- |
| 3 | 15W | 372205 | 2441.818 | 336 | 4 |
| 4 | 20W | 437360 | 2441.843 | 335 | 5 |

LEO先计算金属障碍限定的完整域，再保留长度最多P=k×Dt的区间；Dt=5W。它是区间长度上限，不是左右各移动P。M2/M3的20W为360nm，M4/M5为480nm，M6/M7为640nm。实际域还受障碍缩短。LEO LPA只接受可行的区间约束，未接受的弧仍纳入最终违例，EMA保持已接受约束并压缩长度。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/three_method_extension_comparison.md)。

## 5. 早期GPU投影、200轮差距诊断与回溯尝试

### 5.1 初版连续优化与最佳快照（历史版本H）

当时使用0.2W步长和旧修复策略，按总违例选择历史最佳快照；未包含整数修复或LP/MIP。当前已替换为CPU一致跳跃和末态统计，下列数值只保留历史记录。

| 路径 | 每层预算 | 初始冲突 | 最佳快照冲突 | 残差达到 5nm |
|---|---:|---:|---:|---:|
| CPU | 50 | 104,657 | 86,092 | 4,272 |
| GPU | 50 | 104,657 | 45,949 | 2,080 |
| CPU | 200 | 104,657 | 75,176 | 667 |
| GPU | 200 | 104,657 | 39,159 | 1,044 |

| 路径 | 三次 total_ms | 中位数 |
|---|---|---:|
| CPU | 2165.747, 2231.507, 2160.140 | 2165.747 ms |
| GPU | 681.502, 675.938, 652.318 | 675.938 ms |

| 指标 | CPU | GPU |
|---|---:|---:|
| 最终总冲突 | 75,176 | 39,159 |
| 残差达到 5nm | 667 | 1,044 |
| 优化 total_ms | 8850.580 | 2497.787 |

当时的运行入口如下；同名命令在当前代码上采用新修复规则，不能重现旧轨迹。未记录的早期线长不补造。

```bash
# 仅适用于当时版本；不是当前版本复现命令
./dev-openroad.sh optimize --gui 0 --iterations 50 --backend cpu
./dev-openroad.sh optimize --gui 0 --iterations 50
./dev-openroad.sh optimize --gui 0 --iterations 200 --report /tmp/old_gpu200.csv
```

### 5.2 200/0.25W/35nm旧投影版本

历史最佳快照：CPU总违例72545、≥5nm 577；GPU总违例37263、≥5nm 1036。优化时间中位数分别10.313/2.573秒。真正第200轮末态≥5nm是CPU90、GPU916，说明“最佳总冲突快照”与“第200轮”不是同一统计对象。

```tcl
# 该参数入口属于历史投影版本；当前已不可选择旧投影算法
delgpu -gui 0 -backend cpu -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
delgpu -gui 0 -backend gpu -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
```

### 5.3 临时诊断消融（仅旧诊断补丁有效）

| profile | 变化 | 第200轮≥5nm | 总违例 |
| --- | --- | --- | --- |
| cpu | CPU参考 | 90 | 83142 |
| baseline | 旧GPU投影 | 916 | 82663 |
| backtrack20 | 只恢复20个候选步长 | 916 | 82663 |
| threshold5 | 只改停滞资格为≥5nm | 892 | 83249 |
| barrier_target | 只改CPU跨势垒目标 | 6332 | 60843 |
| projection_aligned | 对齐门槛/调度/清零，保留投影 | 893 | 82739 |
| repair_aligned | CPU跳跃，保留边着色和8候选 | 86 | 81821 |
| aligned_colored | 上一项再恢复20候选 | 86 | 81820 |
| aligned_ordered | 全部CPU语义、原边顺序、立即清零 | 90 | 81728 |
| restored | 恢复旧GPU复测 | 916 | 82663 |

```tcl
# 历史临时补丁上的诊断环境变量；当前主代码没有该切换入口
set ::env(DELGPU_DIAGNOSTIC_PROFILE) aligned_ordered
delgpu -gui 0 -backend gpu -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
unset ::env(DELGPU_DIAGNOSTIC_PROFILE)
# 旧脚本存档（需要对应历史源码/补丁）：
# flow/reports/asap7/ibex/base/delgpu_iteration200_diagnosis/run_diagnostic.tcl
```

诊断结论：主要问题是投影与跨势垒跳跃的语义、触发资格、频率和清零顺序，不能归因于回溯预算一个参数。完全按原边顺序对齐后，200轮≥5nm与CPU全部逐轮一致。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/iteration200_diagnosis.md)。

### 5.4 当前可复现的CPU一致跳跃与0/7/19回溯（C）

回溯次数是初始候选失败后的步长减半重试次数；0表示每种方向只评估初始步长，仍保留原方向及两种固定扰动候选。

| 方法 | 增加量 µm | ≥5nm | 优化时间 s | 命令时间 s |
| --- | --- | --- | --- | --- |
| cpu | 2757.084160 | 90 | 10.197858 | 11.455847 |
| gpu0 | 2764.297657 | 83 | 2.606924 | 3.188175 |
| gpu7 | 2757.134672 | 90 | 3.521841 | 3.982290 |
| gpu19 | 2757.084160 | 90 | 3.068202 | 3.529012 |

```tcl
run_gpu jump200_bt0 -max_iterations 200 -postprocess 0 -learning_rate_mode linear -learning_rate 1 -learning_rate_end 1 -max_backtracks 0
run_gpu jump200_bt7 -max_iterations 200 -postprocess 0 -learning_rate_mode linear -learning_rate 1 -learning_rate_end 1 -max_backtracks 7
run_gpu jump200_bt19 -max_iterations 200 -postprocess 0 -learning_rate_mode linear -learning_rate 1 -learning_rate_end 1 -max_backtracks 19
```

CPU命令见第4节。GPU19与CPU的线长和≥5nm一致，但GPU7略有浮点轨迹差异；GPU0更快，第200轮≥5nm为83。该表没有后处理，小违例未统一列入旧摘要。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/cpu_jump_validation.md)。

## 6. 固定、线性和动态学习率（均关闭后处理）

学习率η乘普通移动距离；它与规则惩罚权重λ不同。λ固定每25轮按10/20/40/80提升。

### 6.1 固定学习率全扫描（LR，单次轨迹检查点）

| 固定η | 检查点轮次 | 增加量 µm | ≥5nm |
| --- | --- | --- | --- |
| 0.5 | 25 | 2369.897996 | 29647 |
| 0.5 | 50 | 2799.703860 | 6931 |
| 0.5 | 75 | 2895.520875 | 3120 |
| 0.5 | 100 | 2922.730880 | 1479 |
| 0.5 | 125 | 2929.857373 | 670 |
| 0.5 | 150 | 2932.327222 | 373 |
| 0.5 | 175 | 2933.584763 | 274 |
| 0.5 | 200 | 2934.307105 | 217 |
| 0.75 | 25 | 2545.219937 | 11824 |
| 0.75 | 50 | 2775.725250 | 2157 |
| 0.75 | 75 | 2808.091366 | 653 |
| 0.75 | 100 | 2818.100877 | 417 |
| 0.75 | 125 | 2821.785960 | 258 |
| 0.75 | 150 | 2825.651652 | 215 |
| 0.75 | 175 | 2826.766005 | 202 |
| 0.75 | 200 | 2828.909219 | 175 |
| 1 | 25 | 2592.436887 | 7530 |
| 1 | 50 | 2731.868445 | 632 |
| 1 | 75 | 2743.725310 | 233 |
| 1 | 100 | 2749.785751 | 124 |
| 1 | 125 | 2753.968850 | 123 |
| 1 | 150 | 2757.966904 | 115 |
| 1 | 175 | 2761.503798 | 99 |
| 1 | 200 | 2764.297657 | 83 |
| 1.25 | 25 | 2583.048606 | 4589 |
| 1.25 | 50 | 2709.494845 | 451 |
| 1.25 | 75 | 2718.204380 | 199 |
| 1.25 | 100 | 2725.128708 | 134 |
| 1.25 | 125 | 2728.112208 | 123 |
| 1.25 | 150 | 2728.961991 | 110 |
| 1.25 | 175 | 2728.961991 | 110 |
| 1.25 | 200 | 2729.049991 | 111 |
| 1.5 | 25 | 2575.095181 | 3449 |
| 1.5 | 50 | 2684.219342 | 336 |
| 1.5 | 75 | 2694.239356 | 236 |
| 1.5 | 100 | 2701.262317 | 138 |
| 1.5 | 125 | 2701.326984 | 140 |
| 1.5 | 150 | 2701.396984 | 143 |
| 1.5 | 175 | 2701.414984 | 142 |
| 1.5 | 200 | 2701.410317 | 142 |
| 2 | 25 | 2564.505112 | 2777 |
| 2 | 50 | 2661.482975 | 388 |
| 2 | 75 | 2678.057959 | 258 |
| 2 | 100 | 2694.110660 | 232 |
| 2 | 125 | 2705.596169 | 223 |
| 2 | 150 | 2710.066096 | 247 |
| 2 | 175 | 2716.174940 | 217 |
| 2 | 200 | 2718.500634 | 218 |
| 3 | 25 | 2652.014907 | 2560 |
| 3 | 50 | 2724.526465 | 608 |
| 3 | 75 | 2758.244865 | 444 |
| 3 | 100 | 2776.525986 | 475 |
| 3 | 125 | 2784.497128 | 358 |
| 3 | 150 | 2791.251245 | 383 |
| 3 | 175 | 2793.992059 | 370 |
| 3 | 200 | 2801.206963 | 436 |
| 4 | 25 | 2786.879234 | 3702 |
| 4 | 50 | 2920.942560 | 1543 |
| 4 | 75 | 2970.703178 | 1008 |
| 4 | 100 | 2984.217940 | 965 |
| 4 | 125 | 2996.058388 | 953 |
| 4 | 150 | 2996.773418 | 958 |
| 4 | 175 | 3001.507708 | 932 |
| 4 | 200 | 3003.146740 | 989 |

```tcl
foreach rate {0.5 0.75 1 1.25 1.5 2 3 4} {
  run_gpu lr_fixed_${rate} -max_iterations 200 -postprocess 0 \
    -learning_rate_mode linear -learning_rate $rate -learning_rate_end $rate
}
```

较大的固定学习率不保证更好：η=1的200轮≥5nm为83，η=4则989。检查点来自同一次200轮轨迹，未单独测每个检查点的独立命令时间。

### 6.2 线性学习率扫描（LR，18次单次探索）

| 线性η | 轮次 | 增加量 µm | ≥5nm |
| --- | --- | --- | --- |
| 1.25→1 | 100 | 2723.840718 | 130 |
| 1.25→1 | 125 | 2727.514932 | 117 |
| 1.25→1 | 150 | 2740.483453 | 90 |
| 1.5→1 | 100 | 2717.368891 | 135 |
| 1.5→1 | 125 | 2716.296368 | 122 |
| 1.5→1 | 150 | 2716.864825 | 85 |
| 2→0.5 | 100 | 2679.420666 | 166 |
| 2→0.5 | 125 | 2697.735728 | 117 |
| 2→0.5 | 150 | 2701.455414 | 97 |
| 2→0.75 | 100 | 2687.499674 | 125 |
| 2→0.75 | 125 | 2699.483746 | 107 |
| 2→0.75 | 150 | 2703.052532 | 89 |
| 2→1 | 100 | 2688.700599 | 122 |
| 2→1 | 125 | 2697.109191 | 128 |
| 2→1 | 150 | 2696.250620 | 121 |
| 3→0.5 | 100 | 2724.086081 | 133 |
| 3→0.5 | 125 | 2753.287975 | 90 |
| 3→0.5 | 150 | 2761.419792 | 112 |

```tcl
run_gpu anneal_1p25_1_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 1.25 -learning_rate_end 1
run_gpu anneal_1p25_1_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 1.25 -learning_rate_end 1
run_gpu anneal_1p25_1_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.25 -learning_rate_end 1
run_gpu anneal_1p5_1_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 1
run_gpu anneal_1p5_1_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 1
run_gpu anneal_1p5_1_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 1
run_gpu anneal_2_0p5_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.5
run_gpu anneal_2_0p5_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.5
run_gpu anneal_2_0p5_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.5
run_gpu anneal_2_0p75_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.75
run_gpu anneal_2_0p75_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.75
run_gpu anneal_2_0p75_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 0.75
run_gpu anneal_2_1_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 1
run_gpu anneal_2_1_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 1
run_gpu anneal_2_1_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 2 -learning_rate_end 1
run_gpu anneal_3_0p5_100 -max_iterations 100 -postprocess 0 -learning_rate_mode linear -learning_rate 3 -learning_rate_end 0.5
run_gpu anneal_3_0p5_125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 3 -learning_rate_end 0.5
run_gpu anneal_3_0p5_150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 3 -learning_rate_end 0.5
```

### 6.3 固定/线性方案三次确认（LR）

| 配置 | 轮次 | η起止 | 增加量 µm | ≥5nm | 优化 s | 命令 s |
| --- | --- | --- | --- | --- | --- | --- |
| baseline200 | 200 | 1.0→1.0 | 2764.297657 | 83 | 2.664178 | 3.117365 |
| fixed150 | 150 | 1.0→1.0 | 2757.966904 | 115 | 1.933327 | 2.409378 |
| fixed125 | 125 | 1.0→1.0 | 2753.968850 | 123 | 1.563488 | 2.027320 |
| anneal150 | 150 | 1.5→1.0 | 2716.864825 | 85 | 1.991256 | 2.446475 |
| anneal125 | 125 | 3.0→0.5 | 2753.287975 | 90 | 1.610964 | 2.105681 |

```tcl
run_gpu lr_baseline200 -max_iterations 200 -postprocess 0 -learning_rate_mode linear -learning_rate 1.0 -learning_rate_end 1.0
run_gpu lr_fixed150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.0 -learning_rate_end 1.0
run_gpu lr_fixed125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 1.0 -learning_rate_end 1.0
run_gpu lr_anneal150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 1.0
run_gpu lr_anneal125 -max_iterations 125 -postprocess 0 -learning_rate_mode linear -learning_rate 3.0 -learning_rate_end 0.5
```

### 6.4 动态学习率：余弦上限 + 停滞/拒绝反馈（AD）

η不增，上限随预算从起始值降到下限；连续两轮梯度拒绝乘0.8，8轮窗口规则势能和严重违例均停滞时乘0.85，均不低于下限。跳跃成功不冒充梯度接受。以下为24组单次筛选。

| 轮次 | 动态η起止 | 增加量 µm | ≥5nm |
| --- | --- | --- | --- |
| 100 | 1.5→0.5 | 2699.161925 | 160 |
| 100 | 1.5→1 | 2693.194958 | 122 |
| 100 | 2→0.5 | 2661.756691 | 130 |
| 100 | 2→1 | 2673.708673 | 156 |
| 100 | 3→0.5 | 2713.638038 | 163 |
| 100 | 3→1 | 2726.645261 | 159 |
| 125 | 1.5→0.5 | 2696.077550 | 105 |
| 125 | 1.5→1 | 2696.669222 | 104 |
| 125 | 2→0.5 | 2670.515235 | 103 |
| 125 | 2→1 | 2679.061165 | 122 |
| 125 | 3→0.5 | 2722.943741 | 115 |
| 125 | 3→1 | 2739.318448 | 157 |
| 150 | 1.5→0.5 | 2693.708982 | 64 |
| 150 | 1.5→1 | 2702.292129 | 92 |
| 150 | 2→0.5 | 2675.403032 | 94 |
| 150 | 2→1 | 2683.346462 | 104 |
| 150 | 3→0.5 | 2730.824584 | 121 |
| 150 | 3→1 | 2741.768863 | 149 |
| 200 | 1.5→0.5 | 2701.158861 | 62 |
| 200 | 1.5→1 | 2712.207883 | 89 |
| 200 | 2→0.5 | 2681.700512 | 76 |
| 200 | 2→1 | 2688.254810 | 112 |
| 200 | 3→0.5 | 2731.962607 | 76 |
| 200 | 3→1 | 2749.305383 | 119 |

```tcl
run_gpu ad_sweep_100_1p5_0p5 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_sweep_100_1p5_1 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 1
run_gpu ad_sweep_100_2_0p5 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 0.5
run_gpu ad_sweep_100_2_1 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 1
run_gpu ad_sweep_100_3_0p5 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 0.5
run_gpu ad_sweep_100_3_1 -max_iterations 100 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 1
run_gpu ad_sweep_125_1p5_0p5 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_sweep_125_1p5_1 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 1
run_gpu ad_sweep_125_2_0p5 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 0.5
run_gpu ad_sweep_125_2_1 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 1
run_gpu ad_sweep_125_3_0p5 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 0.5
run_gpu ad_sweep_125_3_1 -max_iterations 125 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 1
run_gpu ad_sweep_150_1p5_0p5 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_sweep_150_1p5_1 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 1
run_gpu ad_sweep_150_2_0p5 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 0.5
run_gpu ad_sweep_150_2_1 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 1
run_gpu ad_sweep_150_3_0p5 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 0.5
run_gpu ad_sweep_150_3_1 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 1
run_gpu ad_sweep_200_1p5_0p5 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_sweep_200_1p5_1 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 1
run_gpu ad_sweep_200_2_0p5 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 0.5
run_gpu ad_sweep_200_2_1 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 2 -learning_rate_end 1
run_gpu ad_sweep_200_3_0p5 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 0.5
run_gpu ad_sweep_200_3_1 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 3 -learning_rate_end 1
```

动态/线性/固定三次确认：

| 配置 | 模式 | 轮次 | η | 增加量 µm | ≥5nm | 优化 s | 命令 s |
| --- | --- | --- | --- | --- | --- | --- | --- |
| fixed200 | linear | 200 | 1.0→1.0 | 2764.297657 | 83 | 2.654367 | 3.190949 |
| linear150 | linear | 150 | 1.5→1.0 | 2716.864825 | 85 | 1.981703 | 2.474086 |
| matched150 | linear | 150 | 1.5→0.5 | 2713.768792 | 77 | 1.938264 | 2.471367 |
| adaptive150 | adaptive | 150 | 1.5→0.5 | 2693.708982 | 64 | 1.917090 | 2.434139 |
| adaptive200 | adaptive | 200 | 1.5→0.5 | 2701.158861 | 62 | 2.693198 | 3.197783 |

```tcl
run_gpu ad_fixed200 -max_iterations 200 -postprocess 0 -learning_rate_mode linear -learning_rate 1.0 -learning_rate_end 1.0
run_gpu ad_linear150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 1.0
run_gpu ad_matched150 -max_iterations 150 -postprocess 0 -learning_rate_mode linear -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_adaptive150 -max_iterations 150 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
run_gpu ad_adaptive200 -max_iterations 200 -postprocess 0 -learning_rate_mode adaptive -learning_rate 1.5 -learning_rate_end 0.5
```

150轮adaptive 1.5→0.5的连续末态≥5nm为64，长度2693.708982µm。它成为后续默认连续参数；**64不是加整数后处理后的36**。线性1.5→0.5为77，说明收益不只来自改变起止学习率。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/learning_rate_validation.md)。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/adaptive_rate_validation.md)。

## 7. GPU整数后处理与安全回缩

### 7.1 初版后处理阶段对照（P）

同一150轮adaptive/α=.01连续末态，单点/双点修复优先减少≥5nm、所有正残差、残差总量，再考虑线长。回缩要求逐边残差不增并缩短长度。

| 配置 | 增加量 µm | 总违例 | ≥5nm | 小违例 | 后处理 ms | 优化 ms | 命令 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| continuous | 2693.708982 | 80193 | 64 | 80129 | 0.000 | 1868.718 | 2324.360 |
| quantized | 2690.816000 | 5169 | 99 | 5070 | 69.335 | 1984.574 | 2432.708 |
| repaired | 2754.178000 | 84 | 36 | 48 | 98.693 | 2041.086 | 2510.234 |
| full | 2632.331000 | 84 | 36 | 48 | 102.127 | 2000.836 | 2455.702 |

```tcl
run_gpu p_continuous -postprocess 0 -post_retract_mode single
run_gpu p_quantized -post_sweeps 0 -post_pair_rounds 0 -post_retract_sweeps 0 -post_retract_mode single
run_gpu p_repaired -post_retract_sweeps 0 -post_retract_mode single
run_gpu p_full -post_retract_mode single
```

仅取整的违例骤降主要含网格化效应；修复先增加长度来减少违例，再回缩收回冗余。

### 7.2 借鉴DEL polish的协同回缩（R）

加入差分界传播协同回退、合并组的整体最优平移，允许少数节点延长而整组总长度下降。

| 配置 | 增加量 µm | ≥5nm | 小违例 | 回缩 ms | 后处理 ms | 优化 ms | 命令 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| single | 2632.331 | 36 | 48 | 6.344 | 102.535 | 1938.550 | 2420.710 |
| cooperative | 2607.572 | 36 | 48 | 23.955 | 123.667 | 1967.562 | 2421.630 |
| cooperative16 | 2607.525 | 36 | 48 | 28.708 | 128.474 | 2000.242 | 2502.610 |
| cooperative32 | 2607.525 | 36 | 48 | 28.065 | 125.332 | 1992.438 | 2476.589 |

```tcl
run_gpu r_single8 -post_retract_mode single
run_gpu r_cooperative8
run_gpu r_cooperative16 -post_retract_sweeps 16
run_gpu r_cooperative32 -post_retract_sweeps 32
```

cooperative8比single8少24.759µm；16次只再少0.047µm，32次无额外长度收益，保留8次默认预算。所有这些配置均为36/48条，最大残差16nm、残差总量432nm。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/postprocess_validation.md)。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/retraction_validation.md)。

## 8. 将DEL/MIP思路改造成GPU联合区间搜索（J）

用差分约束网络优化固定区间内线长；并行尝试区间候选并作有预算分支定界。没有调用CPU MIP。支持的分量规模/搜索预算有限，大分量回退旧结果，逐边残差保护；不是全局最优保证。

| 配置 | joint轮数×预算 | 增加量 µm | ≥5nm | 小违例 | 联合阶段 ms | 后处理 ms | 命令 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| baseline | 0×16 | 2607.572 | 36 | 48 | 0.000 | 144.368 | 2668.985 |
| network | 1×2 | 2523.814 | 36 | 48 | 329.744 | 480.884 | 2996.885 |
| balanced | 1×16 | 2456.274 | 36 | 48 | 1105.652 | 1254.714 | 3773.323 |
| quality | 1×64 | 2433.808 | 36 | 48 | 2742.547 | 2893.690 | 5421.701 |

```tcl
run_gpu j_baseline -post_joint_rounds 0 -post_joint_trials 16
run_gpu j_network -post_joint_rounds 1 -post_joint_trials 2
run_gpu j_balanced -post_joint_rounds 1 -post_joint_trials 16
run_gpu j_quality -post_joint_rounds 1 -post_joint_trials 64
```

额外预算矩阵是单次探索，不与上表三次中位数合并：

| 配置 | 轮×预算 | 回缩预算 | 增加量 µm | ≥5nm/小违例 | 单次命令 ms |
| --- | --- | --- | --- | --- | --- |
| baseline | 0×16 | 8 | 2607.572 | 36/48 | 2871.622 |
| fixed | 1×2 | 8 | 2523.814 | 36/48 | 3106.974 |
| joint1 | 1×16 | 8 | 2456.274 | 36/48 | 3805.379 |
| joint2 | 2×16 | 8 | 2453.358 | 36/48 | 4825.455 |
| joint4 | 4×64 | 8 | 2430.798 | 36/48 | 12449.528 |
| joint_no_retract | 2×16 | 0 | 2454.237 | 36/48 | 4956.299 |

```tcl
run_gpu js_baseline -post_joint_rounds 0 -post_joint_trials 16 -post_retract_sweeps 8
run_gpu js_fixed -post_joint_rounds 1 -post_joint_trials 2 -post_retract_sweeps 8
run_gpu js_joint1 -post_joint_rounds 1 -post_joint_trials 16 -post_retract_sweeps 8
run_gpu js_joint2 -post_joint_rounds 2 -post_joint_trials 16 -post_retract_sweeps 8
run_gpu js_joint4 -post_joint_rounds 4 -post_joint_trials 64 -post_retract_sweeps 8
run_gpu js_joint_no_retract -post_joint_rounds 2 -post_joint_trials 16 -post_retract_sweeps 0
```

4×64可到2430.798µm，但约12.450秒，边际长度收益小、时间明显增加。1×64长度接近DEL，耗时仍约5.4秒，随后转向LEO式EMA。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/joint_network_validation.md)。

## 9. LEO式EMA：固定区间、跨区间、贪心种子（E）

`ema`：有向紧约束闭包整树平移；`ema_jump`：增加合法区间跨越；`ema_lpa`：在EMA回缩后加入按type/slack/ID贪心选区间的候选，再EMA压缩。候选只有逐边残差不增且更短才接受，一个GPU线程处理一个独立分量，避免逐pivot启动kernel和CPU同步。

本批权重统一0.01；下表三次时间同批，LEO当时默认15W。

| 配置 | 增加量 µm | ≥5nm | 小违例 | 命令 ms |
| --- | --- | --- | --- | --- |
| baseline150 | 2607.572 | 36 | 48 | 2594.670 |
| ema150 | 2523.749 | 36 | 48 | 2606.657 |
| jump150 | 2519.160 | 36 | 48 | 2598.636 |
| seed150 | 2452.195 | 36 | 48 | 2816.137 |
| seed100 | 2453.705 | 36 | 44 | 2138.062 |
| seed75 | 2456.599 | 36 | 48 | 1840.231 |
| mip150 | 2433.808 | 36 | 48 | 5305.886 |
| leo | 2441.818 | 336 | 4 | 2935.619 |

```tcl
run_gpu e_baseline150 -max_iterations 150 -post_retract_mode cooperative -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_ema150 -max_iterations 150 -post_retract_mode ema -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_jump150 -max_iterations 150 -post_retract_mode ema_jump -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_seed150 -max_iterations 150 -post_retract_mode ema_lpa -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_seed100 -max_iterations 100 -post_retract_mode ema_lpa -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_seed75 -max_iterations 75 -post_retract_mode ema_lpa -post_joint_rounds 0 -post_joint_trials 64
run_gpu e_mip150 -max_iterations 150 -post_retract_mode cooperative -post_joint_rounds 1 -post_joint_trials 64
leo -prune_factor 3 -report /tmp/delgpu-all-methods/e_leo15.csv
```

普通EMA150只到2523.749µm；加区间候选后到2452.195µm。把连续预算降到100轮后2453.705µm、36/44、2.138秒，在这一批次比原cooperative150同时更短、更快。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/ema_validation.md)。

## 10. 仅提高线长权重，保留cooperative（W）

目标为 `α×平滑绝对位移 + λ×规则势能`。修改α同步影响梯度、候选能量和接受检查，不改变跳跃规则或后处理；默认α保持0.01。

### 10.1 五组各三次确认

| α | 连续增加量 µm | 连续≥5nm | 连续小违例 | 最终增加量 µm | 最终≥5nm | 最终小违例 | 命令 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0.01 | 2693.709 | 64 | 80129 | 2607.572 | 36 | 48 | 2646.070 |
| 0.05 | 2680.596 | 96 | 83500 | 2603.526 | 36 | 48 | 2676.490 |
| 0.5 | 2590.553 | 91 | 89437 | 2606.694 | 36 | 48 | 2748.157 |
| 1.0 | 2546.188 | 89 | 92592 | 2600.666 | 36 | 52 | 2730.928 |
| 2.0 | 2475.159 | 103 | 95356 | 2613.198 | 36 | 52 | 2704.797 |

```tcl
run_gpu w_0.01 -wire_weight 0.01
run_gpu w_0.05 -wire_weight 0.05
run_gpu w_0.5 -wire_weight 0.5
run_gpu w_1.0 -wire_weight 1.0
run_gpu w_2.0 -wire_weight 2.0
```

### 10.2 全部17组单次权重探索

以下单次不作为稳定时间结论，保留全部原始长度/违例。

| α | 连续增加量 µm | 最终增加量 µm | ≥5nm | 小违例 |
| --- | --- | --- | --- | --- |
| 0.01 | 2693.709 | 2607.572 | 36 | 48 |
| 0.02 | 2690.462 | 2607.955 | 36 | 48 |
| 0.04 | 2681.001 | 2605.948 | 36 | 48 |
| 0.05 | 2680.596 | 2603.526 | 36 | 48 |
| 0.06 | 2677.526 | 2605.649 | 36 | 52 |
| 0.08 | 2670.708 | 2605.698 | 36 | 52 |
| 0.1 | 2662.367 | 2606.987 | 36 | 48 |
| 0.2 | 2634.153 | 2608.419 | 36 | 48 |
| 0.5 | 2590.553 | 2606.694 | 36 | 48 |
| 0.75 | 2559.597 | 2607.772 | 36 | 48 |
| 1.0 | 2546.188 | 2600.666 | 36 | 52 |
| 2.0 | 2475.159 | 2613.198 | 36 | 52 |
| 3.0 | 2438.124 | 2644.049 | 36 | 52 |
| 5.0 | 2412.598 | 2732.013 | 36 | 52 |
| 10.0 | 2386.978 | 2856.401 | 36 | 54 |
| 20.0 | 2282.074 | 2959.997 | 36 | 56 |
| 50.0 | 2177.065 | 3076.413 | 42 | 52 |

```tcl
foreach alpha {0.01 0.02 0.04 0.05 0.06 0.08 0.1 0.2 0.5 0.75 1.0 2.0 3.0 5.0 10.0 20.0 50.0} {
  run_gpu weight_explore_${alpha} -wire_weight $alpha
}
```

结果：α=.05只少4.046µm（0.155%）；α=2连续长度明显减少，修复后补回，最终反而更长；α=50还增加严重违例。权重不是单调的最终线长旋钮。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/wire_weight_validation.md)。

## 11. 在EMA分支上叠加线长权重（EW）

### 11.1 三次确认

同一预算内只改权重；100/150轮差异还含学习率预算调度变化。表中LEO默认是当时15W，leo20是显式20W。

| 配置 | 增加量 µm | ≥5nm | 小违例 | 命令 ms |
| --- | --- | --- | --- | --- |
| cooperative150 | 2607.572 | 36 | 48 | 2553.303 |
| cooperative150_w005 | 2603.526 | 36 | 48 | 2507.411 |
| ema_lpa100_w0.01 | 2453.705 | 36 | 44 | 2039.942 |
| ema_lpa100_w0.02 | 2453.960 | 36 | 44 | 2108.000 |
| ema_lpa100_w0.1 | 2453.232 | 36 | 44 | 2088.373 |
| ema_lpa100_w2 | 2459.079 | 36 | 44 | 2087.995 |
| ema_lpa150_w0.01 | 2452.195 | 36 | 48 | 2710.750 |
| ema_lpa150_w0.02 | 2452.139 | 36 | 48 | 2795.077 |
| ema_lpa150_w0.1 | 2453.009 | 36 | 48 | 2749.927 |
| ema_lpa150_w2 | 2457.201 | 36 | 48 | 2803.122 |
| leo_default | 2441.818 | 336 | 4 | 2908.975 |
| leo20 | 2441.843 | 335 | 5 | 3128.815 |

```tcl
run_gpu ew_ema_lpa100_w0.01 -max_iterations 100 -wire_weight 0.01 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa100_w0.02 -max_iterations 100 -wire_weight 0.02 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa100_w0.1 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa100_w2 -max_iterations 100 -wire_weight 2.0 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa150_w0.01 -max_iterations 150 -wire_weight 0.01 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa150_w0.02 -max_iterations 150 -wire_weight 0.02 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa150_w0.1 -max_iterations 150 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu ew_ema_lpa150_w2 -max_iterations 150 -wire_weight 2.0 -post_retract_mode ema_lpa
```

100轮α=.01→.1只再少0.473µm（0.0193%）；150轮α=.01→.02只少0.056µm（0.0023%），本批耗时均略增，因此未把新权重设为代码默认。

### 11.2 全部46组EMA组合单次探索

表中单次耗时仅存档。第一组explore曾直接写Windows挂载盘，LEO日志有明显I/O开销；不据此作时间排名。refine/modes及确认批次改写Linux/tmp。

| 单次组 | 模式 | 轮次 | α | 增加量 µm | ≥5nm | 小违例 | 单次命令 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| explore | ema_lpa | 100 | 0.01 | 2453.705 | 36 | 44 | 2197.082 |
| explore | ema_lpa | 100 | 0.05 | 2454.029 | 36 | 44 | 2175.250 |
| explore | ema_lpa | 100 | 0.1 | 2453.232 | 36 | 44 | 2320.184 |
| explore | ema_lpa | 100 | 0.2 | 2455.205 | 36 | 44 | 2292.019 |
| explore | ema_lpa | 100 | 0.5 | 2454.460 | 36 | 44 | 3170.531 |
| explore | ema_lpa | 100 | 1.0 | 2456.614 | 36 | 44 | 2256.092 |
| explore | ema_lpa | 100 | 2.0 | 2459.079 | 36 | 44 | 2266.399 |
| explore | ema_lpa | 100 | 5.0 | 2470.222 | 36 | 44 | 2175.611 |
| explore | ema_lpa | 100 | 10.0 | 2474.929 | 36 | 50 | 2157.072 |
| explore | ema_lpa | 150 | 0.01 | 2452.195 | 36 | 48 | 2866.195 |
| explore | ema_lpa | 150 | 0.05 | 2452.571 | 36 | 48 | 2857.319 |
| explore | ema_lpa | 150 | 0.1 | 2453.009 | 36 | 48 | 2892.626 |
| explore | ema_lpa | 150 | 0.2 | 2453.009 | 36 | 48 | 2915.342 |
| explore | ema_lpa | 150 | 0.5 | 2453.643 | 36 | 48 | 2900.987 |
| explore | ema_lpa | 150 | 1.0 | 2453.659 | 36 | 48 | 2870.253 |
| explore | ema_lpa | 150 | 2.0 | 2457.201 | 36 | 48 | 2950.547 |
| explore | ema_lpa | 150 | 5.0 | 2467.974 | 36 | 48 | 3866.560 |
| explore | ema_lpa | 150 | 10.0 | 2474.889 | 36 | 54 | 2762.482 |
| refine | ema_lpa | 100 | 0.02 | 2453.960 | 36 | 44 | 2106.101 |
| refine | ema_lpa | 100 | 0.04 | 2454.009 | 36 | 44 | 2100.979 |
| refine | ema_lpa | 100 | 0.06 | 2454.014 | 36 | 44 | 2094.036 |
| refine | ema_lpa | 100 | 0.08 | 2453.824 | 36 | 44 | 2097.079 |
| refine | ema_lpa | 100 | 0.12 | 2455.069 | 36 | 44 | 2089.478 |
| refine | ema_lpa | 100 | 0.15 | 2454.920 | 36 | 44 | 2071.573 |
| refine | ema_lpa | 150 | 0.02 | 2452.139 | 36 | 48 | 2766.255 |
| refine | ema_lpa | 150 | 0.04 | 2453.283 | 36 | 48 | 3774.345 |
| refine | ema_lpa | 150 | 0.06 | 2453.168 | 36 | 48 | 2722.497 |
| refine | ema_lpa | 150 | 0.08 | 2453.660 | 36 | 48 | 2774.172 |
| refine | ema_lpa | 150 | 0.12 | 2452.646 | 36 | 48 | 2752.140 |
| refine | ema_lpa | 150 | 0.15 | 2453.040 | 36 | 48 | 2736.280 |
| modes | ema | 100 | 0.01 | 2528.503 | 36 | 44 | 1877.362 |
| modes | ema | 100 | 0.1 | 2527.820 | 36 | 44 | 1905.435 |
| modes | ema | 100 | 1.0 | 2560.721 | 36 | 44 | 1879.455 |
| modes | ema | 100 | 5.0 | 2714.271 | 36 | 44 | 1834.400 |
| modes | ema | 150 | 0.01 | 2523.749 | 36 | 48 | 2806.554 |
| modes | ema | 150 | 0.1 | 2525.278 | 36 | 48 | 2500.406 |
| modes | ema | 150 | 1.0 | 2545.158 | 36 | 48 | 2524.152 |
| modes | ema | 150 | 5.0 | 2699.719 | 36 | 48 | 2451.949 |
| modes | ema_jump | 100 | 0.01 | 2523.386 | 36 | 44 | 1857.111 |
| modes | ema_jump | 100 | 0.1 | 2523.180 | 36 | 44 | 1953.221 |
| modes | ema_jump | 100 | 1.0 | 2555.302 | 36 | 44 | 1889.014 |
| modes | ema_jump | 100 | 5.0 | 2705.238 | 36 | 44 | 2804.264 |
| modes | ema_jump | 150 | 0.01 | 2519.160 | 36 | 48 | 2533.046 |
| modes | ema_jump | 150 | 0.1 | 2520.676 | 36 | 48 | 2509.794 |
| modes | ema_jump | 150 | 1.0 | 2540.149 | 36 | 48 | 2511.794 |
| modes | ema_jump | 150 | 5.0 | 2693.173 | 36 | 48 | 2438.860 |

```tcl
run_gpu ews_explore_ema_lpa100_w0.01 -max_iterations 100 -wire_weight 0.01 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w0.05 -max_iterations 100 -wire_weight 0.05 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w0.1 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w0.2 -max_iterations 100 -wire_weight 0.2 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w0.5 -max_iterations 100 -wire_weight 0.5 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w1 -max_iterations 100 -wire_weight 1.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w2 -max_iterations 100 -wire_weight 2.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w5 -max_iterations 100 -wire_weight 5.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa100_w10 -max_iterations 100 -wire_weight 10.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w0.01 -max_iterations 150 -wire_weight 0.01 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w0.05 -max_iterations 150 -wire_weight 0.05 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w0.1 -max_iterations 150 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w0.2 -max_iterations 150 -wire_weight 0.2 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w0.5 -max_iterations 150 -wire_weight 0.5 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w1 -max_iterations 150 -wire_weight 1.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w2 -max_iterations 150 -wire_weight 2.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w5 -max_iterations 150 -wire_weight 5.0 -post_retract_mode ema_lpa
run_gpu ews_explore_ema_lpa150_w10 -max_iterations 150 -wire_weight 10.0 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.02 -max_iterations 100 -wire_weight 0.02 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.04 -max_iterations 100 -wire_weight 0.04 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.06 -max_iterations 100 -wire_weight 0.06 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.08 -max_iterations 100 -wire_weight 0.08 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.12 -max_iterations 100 -wire_weight 0.12 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa100_w0.15 -max_iterations 100 -wire_weight 0.15 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.02 -max_iterations 150 -wire_weight 0.02 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.04 -max_iterations 150 -wire_weight 0.04 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.06 -max_iterations 150 -wire_weight 0.06 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.08 -max_iterations 150 -wire_weight 0.08 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.12 -max_iterations 150 -wire_weight 0.12 -post_retract_mode ema_lpa
run_gpu ews_refine_ema_lpa150_w0.15 -max_iterations 150 -wire_weight 0.15 -post_retract_mode ema_lpa
run_gpu ews_modes_ema100_w0.01 -max_iterations 100 -wire_weight 0.01 -post_retract_mode ema
run_gpu ews_modes_ema100_w0.1 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema
run_gpu ews_modes_ema100_w1 -max_iterations 100 -wire_weight 1.0 -post_retract_mode ema
run_gpu ews_modes_ema100_w5 -max_iterations 100 -wire_weight 5.0 -post_retract_mode ema
run_gpu ews_modes_ema150_w0.01 -max_iterations 150 -wire_weight 0.01 -post_retract_mode ema
run_gpu ews_modes_ema150_w0.1 -max_iterations 150 -wire_weight 0.1 -post_retract_mode ema
run_gpu ews_modes_ema150_w1 -max_iterations 150 -wire_weight 1.0 -post_retract_mode ema
run_gpu ews_modes_ema150_w5 -max_iterations 150 -wire_weight 5.0 -post_retract_mode ema
run_gpu ews_modes_ema_jump100_w0.01 -max_iterations 100 -wire_weight 0.01 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump100_w0.1 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump100_w1 -max_iterations 100 -wire_weight 1.0 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump100_w5 -max_iterations 100 -wire_weight 5.0 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump150_w0.01 -max_iterations 150 -wire_weight 0.01 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump150_w0.1 -max_iterations 150 -wire_weight 0.1 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump150_w1 -max_iterations 150 -wire_weight 1.0 -post_retract_mode ema_jump
run_gpu ews_modes_ema_jump150_w5 -max_iterations 150 -wire_weight 5.0 -post_retract_mode ema_jump
```

普通EMA和EMA区间跳跃叠加大权重同样没有胜过ema_lpa。α=10的ema_lpa100为2474.929µm、36/50，比α=.1更差。给定同一可行区间和残差上限，最小化L与αL的解相同；权重主要通过前面的轨迹和区间落点间接影响末态。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/ema_wire_weight_validation.md)。

## 12. EMA + α=.1，继续缩短轮次（N）

### 12.1 全部19个预算的单次探索

| 轮次 | 增加量 µm | ≥5nm | 小违例 | 最大残差 nm | 单次命令 ms |
| --- | --- | --- | --- | --- | --- |
| 100 | 2453.232 | 36 | 44 | 17.000 | 2084.176 |
| 80 | 2456.888 | 36 | 48 | 17.000 | 1823.748 |
| 75 | 2456.713 | 36 | 48 | 17.000 | 1754.012 |
| 70 | 2458.209 | 36 | 48 | 18.000 | 1715.120 |
| 65 | 2460.032 | 36 | 48 | 18.000 | 1656.127 |
| 60 | 2461.430 | 36 | 48 | 18.000 | 1607.212 |
| 50 | 2464.489 | 36 | 48 | 19.000 | 1491.021 |
| 45 | 2466.820 | 36 | 48 | 19.000 | 1422.272 |
| 40 | 2469.711 | 36 | 48 | 20.000 | 1391.917 |
| 35 | 2472.855 | 36 | 42 | 21.000 | 1353.543 |
| 32 | 2473.671 | 36 | 40 | 21.000 | 1288.530 |
| 30 | 2474.461 | 36 | 40 | 22.000 | 1280.081 |
| 28 | 2475.534 | 36 | 40 | 22.000 | 2443.417 |
| 26 | 2477.916 | 36 | 40 | 23.000 | 1235.702 |
| 25 | 2478.628 | 40 | 38 | 23.000 | 1404.091 |
| 15 | 2484.043 | 52 | 50 | 27.000 | 1101.693 |
| 10 | 2482.968 | 70 | 30 | 29.000 | 1054.931 |
| 5 | 2481.541 | 52 | 34 | 35.000 | 1030.964 |
| 1 | 2479.530 | 42 | 40 | 35.000 | 1872.644 |

```tcl
foreach budget {100 80 75 70 65 60 50 45 40 35 32 30 28 26 25 15 10 5 1} {
  run_gpu iterations_${budget} -max_iterations $budget -wire_weight 0.1 -post_retract_mode ema_lpa
}
```

### 12.2 八个预算三次确认

下表直接取保存的tradeoffs.json；不使用上面单次时间作为提速结论。

| 轮次 | 增加量 µm | 比100轮多 µm | ≥5nm | 小违例 | 最大残差 nm | 命令 s | 时间减少 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 100 | 2453.232 | 0.000 | 36 | 44 | 17 | 2.113 | 0.00% |
| 75 | 2456.713 | 3.481 | 36 | 48 | 17 | 1.776 | 15.98% |
| 60 | 2461.430 | 8.198 | 36 | 48 | 18 | 1.608 | 23.92% |
| 50 | 2464.489 | 11.257 | 36 | 48 | 19 | 1.490 | 29.48% |
| 35 | 2472.855 | 19.623 | 36 | 42 | 21 | 1.325 | 37.32% |
| 30 | 2474.461 | 21.229 | 36 | 40 | 22 | 1.272 | 39.82% |
| 26 | 2477.916 | 24.684 | 36 | 40 | 23 | 1.228 | 41.90% |
| 25 | 2478.628 | 25.396 | 40 | 38 | 23 | 1.264 | 40.18% |

```tcl
run_gpu n_confirm_100 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_75 -max_iterations 75 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_60 -max_iterations 60 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_50 -max_iterations 50 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_35 -max_iterations 35 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_30 -max_iterations 30 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_26 -max_iterations 26 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu n_confirm_25 -max_iterations 25 -wire_weight 0.1 -post_retract_mode ema_lpa
```

50轮是速度/线长折中：比100轮快29.48%，线长多11.257µm、≥5nm仍36、小违例多4、最大残差17→19nm。75轮更保守，快15.98%，长度只多3.481µm。26轮约1.228秒，数量虽仍36，但最大残差23nm；25轮≥5nm增至40。只按≥5nm条数相等不能说质量完全相同。
学习率上限随预算压缩，低预算不是100轮轨迹的简单截断。λ每25轮提升，因此26→25轮还会失去λ从10到20的第一次提升。本次没有另外重设计该调度。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/ema_iteration_budget_validation.md)。

## 13. 完全跳过连续迭代，以及“仅100轮”的阶段解释（Z）

### 13.1 最新同批次100/50/0轮对照

全部为ema_lpa、α=.1、后处理24/8/8、joint=0；零轮直接从原始trim位置进入后处理，后处理内部搜索仍运行。

| 配置 | 增加量 µm | 总违例 | ≥5nm | 小违例 | 最大残差 nm | 连续迭代 ms | 后处理 ms | 命令 s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 100轮 | 2453.232 | 80 | 36 | 44 | 17 | 1144.987 | 345.516 | 2.062 |
| 50轮 | 2464.489 | 84 | 36 | 48 | 19 | 532.147 | 360.113 | 1.470 |
| 仅后处理 | 2478.065 | 80 | 44 | 36 | 35 | 0.000 | 360.105 | 0.985 |

```tcl
run_gpu z100 -max_iterations 100 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu z50 -max_iterations 50 -wire_weight 0.1 -post_retract_mode ema_lpa
run_gpu z0 -max_iterations 0 -postprocess_only 1 -wire_weight 0.1 -post_retract_mode ema_lpa
```

可直接运行的原生命令：

```tcl
delgpu -gui 0 -postprocess_only 1 -wire_weight 0.1 \
  -post_retract_mode ema_lpa -post_joint_rounds 0
```

```bash
./dev-openroad.sh optimize --gui 0 --postprocess-only 1 --wire-weight 0.1 \
  --post-retract-mode ema_lpa --post-joint-rounds 0
```

仅后处理耗时0.985秒，比100轮快52.23%、比50轮快32.98%。增加量比100轮多24.833µm；总违例虽然同为80，但严重违例多8条、最大残差17→35nm。权重只影响连续迭代，因此在零轮路径中不影响后处理的移动选择。

`-postprocess_only 1` 要求GPU和后处理开启；省略max_iterations时自动用0，显式传轮数必须为0。**单独 `-max_iterations 0` 仍只是建图，不是执行零轮后处理。**

### 13.2 仅100轮连续迭代，关闭后处理

```tcl
delgpu -gui 0 -max_iterations 100 -wire_weight 0.1 -postprocess 0
```

| 阶段 | 增加量 µm | 总违例 | ≥5nm | 小违例 | 最大残差 nm |
| --- | --- | --- | --- | --- | --- |
| 原始位置 | 0.000 | 104657 | 100276 | 4381 | 78.000 |
| 仅100轮连续迭代 | 2674.271 | 84473 | 150 | 84323 | 50.173 |
| 整数化 | 2670.554 | 6286 | 180 | 6106 | 50.000 |
| 整数修复 | 2733.887 | 80 | 36 | 44 | 17.000 |
| EMA回缩 | 2453.232 | 80 | 36 | 44 | 17.000 |
| 最终独立整数检查 | 2453.232 | 80 | 36 | 44 | 17.000 |

另行关闭后处理实测并由CPU独立复核：增加量2674.270839µm、84473条总违例、150条≥5nm、84323条小违例。该单次优化total_ms=1288.715ms、迭代1144.810ms；没有测预热后三次完整命令中位数，不能把1.289秒放入上表命令时间列。
连续迭代优化有限权重的软约束势能，线长回拉与规则梯度平衡时可留下小正残差。原始100276条严重违例大部分压到5nm以下；例如要求距离≥90nm而结果89.99nm，仍统计0.01nm正残差。整数化消除大量极小残差，也可能新生违例，之后精确整数修复，再由EMA压缩长度。故84473是纯连续阶段结果，80是后处理后结果，不能混为同一个“100轮DRC”。
来源：[详细记录](tools/OpenROAD/src/delgpu/doc/postprocess_only_validation.md)。

## 14. LEO旁路诊断（仓库已有分析产物，未接入delgpu）

这部分与上述delgpu实验分开：LEO未接受的合法区间组合是短线长/残留违例的重要限制。诊断工具不改变LEO求解器、不写回ODB，不作为GPU加速实测。

| LEO k | 范围 | 保留边数 | 总违例 | ≥5nm | 增加量 µm |
| --- | --- | --- | --- | --- | --- |
| 3 | 15W | 372205 | 340 | 336 | 2441.818 |
| 4 | 20W | 437360 | 340 | 335 | 2441.843 |
| 6 | 30W | 546524 | 340 | 335 | 2441.886 |
| 8 | 40W | 634639 | 340 | 335 | 2441.964 |

k=3残留340条中，64条在完整金属障碍域内单弧也不可行，276条单弧可行但仍受联合区间选择影响。64只是该模型全部正残差数量的下界，不是≥5nm数量下界。独立MILP对249个残留子图、保护所有原合法弧，所得候选总违例76、全部≥5nm、增加量2454.434µm；比原LEO只多12.616µm，消除264条C弧。局部MILP约12.905秒，不含建图/导出/复核，不能与完整命令时间比较，也不代表GPU已实现该候选。
运行命令（前两段在tools/OpenROAD目录）：

```bash
OPENROAD_CUDA_PATH=/home/karl/.local/opt/cuda-12.9.1 bazelisk build --jobs=8 \
  --//:platform=gui --//src/delgpu:gpu=cuda //src/leo/analysis:diagnose_conflicts
```

```bash
mkdir -p /tmp/leo-conflict-audit
LEO_DIAGNOSTIC_COMPONENTS=/tmp/leo-conflict-audit/components.jsonl \
  bazel-bin/src/leo/analysis/diagnose_conflicts \
  ../../flow/results/asap7/ibex/base/5_2_route.odb \
  /tmp/leo-conflict-audit/summary.csv /tmp/leo-conflict-audit/details.csv 3 4 6 8
```

```bash
python3 -m pip install --target /tmp/leo-mip-audit numpy==2.2.6 scipy==1.15.3
PYTHONPATH=/tmp/leo-mip-audit OPENBLAS_NUM_THREADS=1 python3 \
  src/leo/analysis/check_joint_feasibility.py \
  /tmp/leo-conflict-audit/components.jsonl /tmp/leo-conflict-audit/joint_result.json
```

```bash
python3 - <<'PY'
import csv, json
root = '/tmp/leo-conflict-audit/'
results = json.load(open(root + 'joint_result.json'))
with open(root + 'candidate_positions.csv', 'w') as stream:
    writer = csv.writer(stream)
    writer.writerow(['layer', 'node', 'position2'])
    for component in results['components']:
        for node, position in component.get('positions', {}).items():
            writer.writerow([component['layer'], node, position])
PY
LEO_DIAGNOSTIC_CANDIDATE=/tmp/leo-conflict-audit/candidate_positions.csv \
  bazel-bin/src/leo/analysis/diagnose_conflicts \
  ../../flow/results/asap7/ibex/base/5_2_route.odb \
  /tmp/leo-conflict-audit/verified_baseline.csv \
  /tmp/leo-conflict-audit/verified_details.csv 3
```

约束和C++复核见 [LEO独立诊断](tools/OpenROAD/src/leo/doc/conflict_tradeoff_audit.md)。这不是对当前所有节点/几何自由度的无条件全局最优结论。

## 15. 批量脚本与归档索引

下表是现有批量入口。性能报告先写/tmp；更换输出目录防止覆盖旧样本。当前参数复现优先使用第3节的显式模板，因为部分旧脚本依赖旧默认值。

| 内容 | 详细记录 | 脚本（src/delgpu/scripts/） | 汇总器 |
| --- | --- | --- | --- |
| graph | graph_validation.md | — | — |
| CPU跳跃/回溯 | cpu_jump_validation.md | benchmark_cpu_jump.tcl | summarize_cpu_jump.py |
| 固定/线性学习率 | learning_rate_validation.md | benchmark_learning_rate.tcl | summarize_learning_rate.py |
| 动态学习率 | adaptive_rate_validation.md | benchmark_adaptive_rate.tcl | summarize_adaptive_rate.py |
| 初版后处理 | postprocess_validation.md | benchmark_postprocess.tcl | summarize_postprocess.py |
| 协同回缩 | retraction_validation.md | benchmark_retraction.tcl | summarize_retraction.py |
| LEO/DEL/GPU | three_method_extension_comparison.md | compare_wire_extension.tcl | summarize_three_methods.py |
| 联合搜索 | joint_network_validation.md | benchmark_joint.tcl | summarize_joint.py |
| EMA模式 | ema_validation.md | benchmark_ema.tcl | summarize_ema.py |
| 单独权重 | wire_weight_validation.md | benchmark_wire_weight.tcl | summarize_wire_weight.py |
| EMA+权重、轮次、零轮 | postprocess_only_validation.md | benchmark_ema_weight.tcl | summarize_ema_weight.py |

已核对的历史漂移：旧 `benchmark_cpu_jump.tcl` 的循环没有显式关闭GPU后处理，不能直接用当前默认值复现纯连续C批次；用第5.4节显式命令。旧EMA脚本调用裸 `leo`，当前默认已由15W改为20W，复现历史E批次LEO行须显式 `-prune_factor 3`。历史诊断profile入口已移除，不能仅设置旧环境变量就声称切换了算法。
常用批量复现（当前版本）：

```bash
# EMA + 权重三次对照
mkdir -p /tmp/ema-weight-rerun
DELGPU_BENCHMARK_DIR=/tmp/ema-weight-rerun \
  DELGPU_WIRE_WEIGHTS='0.01 0.02 0.1 2' DELGPU_ITERATION_BUDGETS='100 150' \
  ./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_ema_weight.tcl \
  > /tmp/ema-weight-rerun/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_ema_weight.py /tmp/ema-weight-rerun

# α=.1缩短轮次三次对照；DELGPU_SAMPLES=1可改成单次探索
mkdir -p /tmp/ema-iterations-rerun
DELGPU_BENCHMARK_DIR=/tmp/ema-iterations-rerun DELGPU_WIRE_WEIGHTS=0.1 \
  DELGPU_ITERATION_BUDGETS='100 75 60 50 35 30 26 25' \
  ./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_ema_weight.tcl \
  > /tmp/ema-iterations-rerun/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_ema_weight.py /tmp/ema-iterations-rerun

# 100/50/零轮三次对照：脚本在预算为0时自动打开postprocess_only
mkdir -p /tmp/ema-zero-rerun
DELGPU_BENCHMARK_DIR=/tmp/ema-zero-rerun DELGPU_WIRE_WEIGHTS=0.1 \
  DELGPU_ITERATION_BUDGETS='100 50 0' \
  ./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_ema_weight.tcl \
  > /tmp/ema-zero-rerun/benchmark.log 2>&1
python3 tools/OpenROAD/src/delgpu/scripts/summarize_ema_weight.py /tmp/ema-zero-rerun
```

### 数据目录（均相对于ORFS根目录）

| 目录 | 状态 |
| --- | --- |
| flow/reports/asap7/ibex/base/del_native_200_025_35/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_defaults_200_025_35/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_iteration200_diagnosis/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_cpu_jump_200/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_learning_rate/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_adaptive_rate/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_postprocess/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_retraction/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_three_methods/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_joint/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_ema/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_wire_weight/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_ema_weight/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_ema_iterations/ | 本地存在 |
| flow/reports/asap7/ibex/base/delgpu_postprocess_only/ | 本地存在 |
| flow/reports/asap7/ibex/base/leo_conflict_tradeoff/ | 本地存在 |

近期目录中保留CSV、summary/comparison/tradeoffs JSON、日志、sha256、GUI截图和验证脚本；这些大型原始产物通常被Git忽略。历史报告和sha256描述各自测量时的源码/二进制，不要求与后续改动后的工作区一致。

## 16. 到此为止的结论与已验证内容

| 尝试 | 观察到的结果 | 取舍 |
| --- | --- | --- |
| GPU建图 | CPU/GPU逐项一致，719.708→447.944ms | 仅建图约1.61倍，不等于全流程 |
| CPU一致跳跃+关闭回溯 | 200轮GPU0为83条≥5nm，优化约2.607s | 提高速度；浮点轨迹与CPU可不同 |
| 固定/线性/动态学习率 | adaptive150把连续≥5nm降至64 | 连续软约束末态仍有大量小正残差 |
| 整数修复+single回缩 | 36/48条，2632.331µm | GPU实现明确残差修复 |
| cooperative协同回缩 | 2607.572µm | 比single少24.759µm；再加预算收益小 |
| GPU联合网络/分支 | 1×64为2433.808µm，约5.4s | 更接近DEL长度，时间增加；4×64约12.45s |
| LEO式EMA+贪心区间候选 | 100轮α=.01为2453.705µm、36/44、2.138s | 避免昂贵联合网络求解，但非全局最优 |
| 单独/EMA叠加线长权重 | 单独α=.05改善4.046µm；EMA100α=.1仅再少.473µm | 大权重会导致修复补偿，收益不单调 |
| α=.1缩短到50轮 | 本批约1.49s，2464.489µm、36/48 | 比100轮约快29.5%；最大残差17→19nm |
| α=.1缩短到26轮 | 本批约1.228s，2477.916µm、36/40 | 最大残差23nm，数量相等不代表质量相同 |
| 仅后处理 | 最新批0.985s，2478.065µm、44/36 | 比100轮快52.2%；最大残差35nm |
| 原版DEL LP/MIP | 2423.544µm、36/40 | 单次命令约76s；未做三次稳定性能比较 |
| LEO默认统一20W | 2441.843µm、335/5 | 短线长伴随更多未解决图约束 |

校验记录包括：CPU/GPU建图一致性；近期GPU模块91项回归保持通过；LEO7组测试；仅建图和仅后处理入口区分；无连续迭代/候选的零轮断言；100/50正常路径与改动前非计时CSV一致；GUI显示更新及关闭覆盖层；独立CPU整数末态检查、硬域、blocked和实际dbWire原长检查。最新91项回归使用了Bazel缓存，新增零轮入口另有真实IBEX与GUI端到端验证。

本文件保存的是全部已记录尝试与可复现入口。没有把未记录的时间、旧版本未测线长或未执行的方案填成结果。

### 本次整理覆盖数量

| 数据集合 | 条目数 |
| --- | --- |
| fixed_learning_rate_checkpoints | 64 |
| linear_learning_rate_explorations | 18 |
| adaptive_learning_rate_explorations | 24 |
| wire_weight_single_explorations | 17 |
| ema_weight_single_explorations | 46 |
| iteration_budgets_explored | 19 |
