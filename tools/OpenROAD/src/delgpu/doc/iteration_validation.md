# GPU 迭代验证记录（2026-10-14）

设备：RTX 5060 Laptop GPU，CUDA 12.9 Runtime / 13.1 driver，Clang 22。
输入：ORFS asap7/ibex/base/5_2_route.odb，M2–M7，332,664 节点、437,360 边。
图与规则、步长 0.2W、lambda 调度保持相同。CPU 是移植的 DEL 连续迭代路径，
没有运行 LP/MIP 后处理；两种后端均最终返回冲突最少的快照。

## 正确性和质量

- 56 项测试通过：原 DEL 的几何、节点、移动域、建边、CPU 迭代测试；GPU 随机建图；
  GPU 单步对照、B/C 中点及负距离、blocked、共享端点、最佳快照和自定义合法区间。
- 六层建图逐项 CPU/GPU 相同；迭代允许不同路径。
- 最终坐标由独立 CPU 计算检查域、blocked、所有 ArcRange 及残余冲突。
- 200 轮 CSV 中每层最终快照的冲突数均等于该层所有记录中的最小值。
- offscreen Qt 图像测试确认迭代后覆盖层发生变化，关闭 GUI 清除覆盖层。

| 路径 | 每层预算 | 初始冲突 | 最佳快照冲突 | 残差达到 5nm |
|---|---:|---:|---:|---:|
| CPU | 50 | 104,657 | 86,092 | 4,272 |
| GPU | 50 | 104,657 | 45,949 | 2,080 |
| CPU | 200 | 104,657 | 75,176 | 667 |
| GPU | 200 | 104,657 | 39,159 | 1,044 |

这里的“冲突”是连续坐标的规则残差超过 1e-6 DBU；不是已写回布线的 DRC 数量。
图像按整数 DBU 舍入显示，不能代替连续坐标的约束统计。
仍有冲突，不宣称零违规或全局最优；当前未包含整数合法化与 LP/MIP 后处理。

## 性能

关闭 GUI、校验和 CSV，一次 GPU 预热后在同一 OpenROAD 进程中交替执行各后端三次，
每层预算为 50。优化 total_ms 含 CSR/着色初始化、设备上传/工作区分配、全部迭代、
最佳快照恢复、最终下载和主机检查，排除读库及建图。

| 路径 | 三次 total_ms | 中位数 |
|---|---|---:|
| CPU | 2165.747, 2231.507, 2160.140 | 2165.747 ms |
| GPU | 681.502, 675.938, 652.318 | 675.938 ms |

本样例连续优化约 3.20 倍加速；轨迹和修复策略不同，不能视作严格等工作量核比较。
GPU 200 轮（开启 CSV）优化耗时 2536.529 ms。
GPU 每次仅为当前层保留设备状态，最大层记录 90.84 MiB 工作缓冲；不含 CUDA 上下文。
无 GUI 时完整坐标下载总计 2,661,312 字节，即各层最终快照各一次。
每轮及每个候选仍读取小统计摘要，未把这些摘要字节计入坐标下载量。

复现：

```bash
./dev-openroad.sh optimize --gui 0 --iterations 50 --backend cpu
./dev-openroad.sh optimize --gui 0 --iterations 50
./dev-openroad.sh optimize --gui 0 --iterations 200 --report /tmp/delgpu.csv
QT_QPA_PLATFORM=offscreen ./dev-openroad.sh run -gui -no_settings -exit \
  tools/OpenROAD/src/delgpu/test/gui_optimizer.tcl
```

## CPU/GPU 200 轮补充比较

同一 ibex 输入、GUI/校验关闭、步长 0.2W、CSV 开启，分别执行一次 200 轮。
程序最终返回各层最佳快照，统计使用相同的最终主机残差检查。

| 指标 | CPU | GPU |
|---|---:|---:|
| 最终总冲突 | 75,176 | 39,159 |
| 残差达到 5nm | 667 | 1,044 |
| 优化 total_ms | 8850.580 | 2497.787 |

这次单次测量 GPU 优化约 3.54 倍加速，总冲突比 CPU 少约 47.9%。
CPU 的 >=5nm 冲突更少，GPU 未在所有质量指标上胜出。
当前快照优先比较总冲突数，再比较最大残差和总位移，因此总冲突数与较大残差数存在取舍。
本比较排除读库、建图和 LP/MIP 后处理；不是完整 RTL-to-GDS 时间或最终布线 DRC。


## 新默认参数

上述 0.2W/5nm 数据保留为历史实验。默认入口现已改为 200 轮、0.25W、35nm，
新测试结果见 [默认参数验证](default_parameters_validation.md)。
