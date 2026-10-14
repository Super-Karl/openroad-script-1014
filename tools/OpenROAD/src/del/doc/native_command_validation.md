# 原版 DEL 指定命令验证（2026-10-15）

本次使用新拷入 `tools/OpenROAD/src/del` 的完整实现，接入 Bazel 主程序。
没有改变 DEL 建图、连续优化或后处理算法。Bazel 使用现有 OR-Tools 9.15，HiGHS 开启。
输入为同一 asap7/ibex/base/5_2_route.odb，332,664 节点、437,360 边；GUI 关闭。

严格执行原命令，未额外加入 `-report` 或修改 `-frame_ms`：

```tcl
del -step -max_iterations 200 -max_step 0.25 -jump_offset_nm 35
c
```

`-step` 先停在第 0 轮，`c` 完成 200 轮，并自动执行端点压缩与整数冲突合法化。

| 阶段 | 严格正残差冲突 | 残差达到 5nm |
|---|---:|---:|
| 初始 | 104,657 | 100,276 |
| 连续第 200 轮末态 | 83,142 | 90 |
| LP/MIP 后处理最终 | 76 | 36 |

最终 A=57、B=19、C=0，其中 40 个冲突低于 5nm，仍计入 76。
M2 为 72（A=54、B=18），M6 为 4（A=3、B=1），M3/M4/M5/M7 为 0。
各分量求解均报告最优：nonoptimal_components=0；polish_optimal=true。
结果是 trim 覆盖层的约束统计，不是实际布线 DRC；原始 ODB 未写回，
前后普通布线长度同为 89,786,472 DBU。

## 时间

- 第 200 轮日志累计 elapsed：12.104 秒。
- 各层后处理计时：31.288 / 15.972 / 4.085 / 1.869 / 1.939 / 0.498 秒。
- Tcl 墙钟：初始化/建图/第 0 轮 0.809486 秒；`c` 75.755954 秒；合计 76.565440 秒。
- 阶段日志计时没有覆盖全部调度、统计、快照等开销；完整过程以墙钟合计为准。
- 数据库加载发生在开始计时之前，编译时间不计入运行时间。

## 与此前 GPU 数据的关系

此前 GPU 200 轮、0.25W/35nm 的最佳快照为 37,263 个冲突，达到 5nm 的为 1,036，
优化中位数 2.572529 秒。该 GPU 路径尚未接入 LP/MIP 后处理，并且会恢复历史最佳快照。
原版 DEL 使用第 200 轮末态进入后处理。因此不能把 GPU 的连续阶段直接等同于
原版 DEL 的 76 个最终冲突；此前 CPU 参考后端的 72,545 也是最佳快照，不是原版末态。

复现：

```bash
./dev-openroad.sh build
./dev-openroad.sh run -no_splash -exit tools/OpenROAD/src/del/scripts/run_requested.tcl
```

日志与机器可读汇总位于 `flow/reports/asap7/ibex/base/del_native_200_025_35/`。
