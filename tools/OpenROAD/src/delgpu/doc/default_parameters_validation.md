# 默认参数 200 / 0.25W / 35nm 验证

日期：2026-10-14。输入仍为 asap7/ibex/base/5_2_route.odb，M2–M7。
应用默认值位于 `include/delgpu/Parameters.h`，Tcl 命令和启动脚本同步更新。
CPU 是 DEL 连续迭代参考后端，直接接收 max_step=0.25、jump_offset_nm=35。
GPU 优先按边着色投影到合法区间；合法目标不可达时，B/C 跨势垒候选使用
`midpoint ± 35nm`，按当前层线宽和 DBU 换算。两个后端仍采用不同修复策略。

构建及 57 项测试通过；新增测试验证 5/35nm 偏移产生不同 GPU 跳跃目标，
以及 DBU 比例为 1000/2000 时的物理单位一致性和硬域约束。
GUI 回归检查确认新参数下的迭代图像更新和关闭显示。

## 性能和质量

关闭 GUI/建图校验，开启 CSV。一次 GPU 200 轮预热后，同一进程中交替运行
CPU/GPU 各三次。直接省略算法参数调用 `delgpu`，日志确认每次实际参数均为
max_iterations=200、max_step=0.25W、jump_offset_nm=35。

| 指标 | CPU | GPU |
|---|---:|---:|
| 初始冲突 | 104,657 | 104,657 |
| 最佳快照总冲突 | 72,545 | 37,263 |
| 残差达到 5nm | 577 | 1,036 |
| 优化耗时中位数 | 10,312.957 ms | 2,572.529 ms |

三次分别为：

- CPU：10,178.586 / 10,312.957 / 10,568.359 ms。
- GPU：2,492.470 / 2,620.195 / 2,572.529 ms。

当前方法比较约 4.01 倍加速，GPU 总冲突比 CPU 少 48.63%。
CPU 的 >=5nm 残差更少，两个质量指标存在取舍。
计时包含优化初始化、迭代、最佳快照恢复、最终下载和主机检查，排除读库、建图、
CUDA 上下文初始化和 LP/MIP 后处理。总冲突阈值为 1e-6 DBU，结果不是实际布线 DRC。

六个 CSV 均包含每层完整 200 轮记录和最终最佳快照。
GPU 各层最终冲突数均等于本次记录的最小值；各次最终质量统计一致。

复现：

```bash
./dev-openroad.sh optimize --backend cpu --gui 0
./dev-openroad.sh optimize --gui 0
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/delgpu/scripts/benchmark_defaults.tcl
```

显式覆盖：

```tcl
delgpu -backend cpu -gui 0 -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
delgpu -backend gpu -gui 0 -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
```

完整日志、CSV 和机器可读汇总保存在
`flow/reports/asap7/ibex/base/delgpu_defaults_200_025_35/`。
