# main 最终方案：100 轮浮点迭代 + GPU EMA

日期：2026-10-16。由 `tmp-gpu-ema / 5a137a0` 恢复完整 EMA 实现到 main，撤回后续纯整数迭代实验，统一正式默认配置。

## 最终默认值

| 参数 | 默认值 |
|---|---|
| 后端 / GUI / 建图校验 | GPU / 0 / 0 |
| 移动域 | 20W |
| 连续迭代轮数 | 100 |
| 普通最大步长 / 跳跃偏移 | 0.25W / 35nm |
| 学习率 | adaptive，1.5→0.5 |
| GPU 线长权重 | 0.1 |
| 步长减半重试 | 0 |
| 整数修复 / 双端点轮数 | 24 / 8 |
| 回缩方式 / EMA预算 | ema_lpa / 8×分量节点数 |
| 联合区间搜索 | 0，关闭 |
| 仅后处理 | 0，关闭 |

流程为：构图 → 100 轮浮点迭代 → 整数化和冲突修复 → GPU EMA 压缩 → 独立整数几何检查 → 按需 GUI 显示。
这里的 EMA 是带贪心区间候选的 `ema_lpa`，保留原始逐边残差保护。
保留既有整数后处理，不再提供“100 轮之后追加纯整数迭代”的 `-integer_iterations` 入口。

C++ 的 `Parameters.h`、`DelGpu::run()` 默认值、Tcl 和 `dev-openroad.sh optimize` 已统一。
无 GUI 时不保留显示副本，不逐轮下载坐标；各层只下载一次最终快照，全设计共 2661312 字节。
LEO 的默认移动域也保留为 20W（`prune_factor=4`）。CPU 参考仍使用原算法和 0.01 线长权重；应用默认迭代预算同步为 100，需要 200 轮对照时显式指定。

## 直接运行

```bash
cd /mnt/e/202610/code/OpenROAD-flow-scripts
./dev-openroad.sh build
./dev-openroad.sh optimize
./dev-openroad.sh optimize --gui 1 --report /tmp/ema100.csv
```

已加载设计的 OpenROAD Tcl 终端：

```tcl
delgpu
# 显示结果需已开启 GUI：
delgpu -gui 1
```

完整显式参数：

```tcl
delgpu -gui 0 -verify 0 -backend gpu -max_iterations 100 -max_step 0.25 \
  -jump_offset_nm 35 -max_backtracks 0 -learning_rate_mode adaptive \
  -learning_rate 1.5 -learning_rate_end 0.5 -wire_weight 0.1 \
  -postprocess 1 -postprocess_only 0 -post_sweeps 24 -post_pair_rounds 8 \
  -post_retract_sweeps 8 -post_retract_mode ema_lpa \
  -post_joint_rounds 0 -post_joint_trials 16 -report /tmp/ema100.csv
```

## 默认值复测

同一输入 `flow/results/asap7/ibex/base/5_2_route.odb`，SHA256 为
`381e1ddc86e2e9f1017fcd1094d11b969efa8fee40d32c50ff6d0f082ab4d744`。
1000 DBU/µm；M2–M7，共 332664 节点、437360 条边。
先预热，默认与显式参数交替执行各三次，GUI / verify 关闭；不同时运行其他 GPU 测试。

| 指标 | 所有默认样本与显式样本 |
|---|---:|
| 线长增量 | 2453.232µm |
| ≥5nm 违例 | 36 |
| 0<残差<5nm 违例 | 44 |
| 全部违例 | 80 |
| 最大残差 | 17nm |
| 残差总量 | 500nm |
| 默认命令时间中位数 | 2.149229 秒 |
| 显式命令时间中位数 | 2.170525 秒 |

默认三次命令时间为 2237.975, 2132.29, 2149.229 ms；显式为 2112.516, 3747.785, 2170.525 ms。
保留慢样本，不剔除 3747.785ms 的显式运行。阶段中位数不机械相加。
命令时间包含建图、初始化、100 轮迭代、后处理、报告和最终检查，不含 read_db、进程启动及预热。
线长是 trim 模型位移/净延长量，违例是 A/B/C 约束图残差，不是完整物理 DRC。
每次执行前后普通 dbWire 长度均保持 89786.472µm，未修改实际布线。

六次样本的 **636 行所有非计时列逐项相同**，并与归档版本的 EMA100、权重 0.1 CSV 完全一致。
当前仅 100 轮连续末态、取整、修复及 EMA 末态均得到复现，不只是最终汇总相同。

复现与归档：

```bash
./dev-openroad.sh run -no_splash -exit \
  tools/OpenROAD/src/delgpu/scripts/benchmark_final_ema100.tcl \
  > /tmp/delgpu-final-ema-benchmark.log 2>&1
```

`DELGPU_BENCHMARK_DIR` / `DELGPU_ROUTE_DB` 可覆盖输出目录 / 输入数据库。
实际产物在 `flow/reports/asap7/ibex/base/delgpu_final_ema100/`：默认及显式 CSV、summary JSON、命令/编译/测试日志、GUI 截图与 SHA256。

## 回退与验证

- 已移除整数迭代的 C++ / CUDA / Tcl / 启动脚本入口及对应实验脚本，未知 `-integer_iterations` 参数实际验证为拒绝。
- 整数实验源码和文档以 tar.gz 归档在
  `flow/reports/asap7/ibex/base/delgpu_integer_iteration/rollback_snapshot/integer_iteration_source.tar.gz`；原始 CSV、图像和运行日志保留。
- 归档含回退前所有 13 个已修改文件、3 个新文件、tracked patch 与清单；未把已放弃的整数算法重新加入正式源码。
- CUDA 模块及 OpenROAD 宿主编译通过，GPU 单元测试 **91/91** 通过。CPU 对齐测试显式使用旧参考权重 0.01，避免把参数变化误当作算法差异。
- `./dev-openroad.sh optimize` 不指定算法参数时与默认 Tcl 命令结果完全相同。
- GUI 默认参数结果与 headless 相同；截图确认最终覆盖层更新，`-gui 0` 清除覆盖层。
- 修改通过 C++ 格式化、`bash -n`、`git diff --check`。
- 原有 abc 子模块本地状态与已有 stash 保持原样；没有修改 OpenSTA。

旧 150 轮 / cooperative / 权重 0.01 的文档和图文汇报保留为历史实验资料；当前默认值以本页和 README 顶部为准。
