# 只读残留违例诊断

该目录是独立分析工具，不接入 `leo` Tcl 命令，不改变 baseline 算法、不写 ODB。
2026-10-16 的结果和约束假设见 [诊断记录](../doc/conflict_tradeoff_audit.md)。

在 `tools/OpenROAD` 构建 C++ 工具：

```bash
OPENROAD_CUDA_PATH=/home/karl/.local/opt/cuda-12.9.1 bazelisk build --jobs=8 \
  --//:platform=gui --//src/delgpu:gpu=cuda //src/leo/analysis:diagnose_conflicts
```

按与 `Leo::buildGraph` 相同的抽取顺序和规则重建图，调用原有 `solveLpa`、`solveEma`，
再按弧的可达差值区间与合法区间是否相交分类。k=3 自动核对存档 baseline 的
节点数、弧数、违例数和延长量。不计时比较该工具与 OpenROAD 命令：它省略了 UI、
Tcl 和逐子图日志，目的仅是可行性诊断。

```bash
mkdir -p /tmp/leo-conflict-audit
LEO_DIAGNOSTIC_COMPONENTS=/tmp/leo-conflict-audit/components.jsonl \
  bazel-bin/src/leo/analysis/diagnose_conflicts \
  ../../flow/results/asap7/ibex/base/5_2_route.odb \
  /tmp/leo-conflict-audit/summary.csv /tmp/leo-conflict-audit/details.csv 3 4 6 8
```

可选环境变量 `LEO_DIAGNOSTIC_COMPONENTS` 导出 k=3 下含未接受弧的完整子图，JSONL
保留节点整数域、原始和最终坐标、权重、全部弧及合法区间。

独立联合求解需要 numpy/scipy，可安装到临时目录，避免修改项目环境：

```bash
python3 -m pip install --target /tmp/leo-mip-audit numpy==2.2.6 scipy==1.15.3
PYTHONPATH=/tmp/leo-mip-audit OPENBLAS_NUM_THREADS=1 python3 \
  src/leo/analysis/check_joint_feasibility.py \
  /tmp/leo-conflict-audit/components.jsonl /tmp/leo-conflict-audit/joint_result.json
```

脚本先最大化满足弧的数量，再在该数量下最小化延长量；原本合法的弧必须继续合法，
但其区间选择可以改变。每次 MILP 限时 2 秒，整个诊断限时 120 秒；只有 status=0
的子问题报告求解器最优，超时或未完成时不能当成全体最优结果。
候选会用舍入后的整数坐标独立逐弧复核，不允许原本合法的弧变成违例。

导出求解所得坐标供 C++ 再复核：

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

复核结果写到 `candidate_positions.csv.checked.csv`。未覆盖的节点维持 baseline 坐标。
工具使用原 C++ `assessLayer` 检查金属域、所有弧的合法区间并集和延长量；另将域扩为
原位置 ±P 后重新生成更广的候选弧，确认剪枝没有掩盖候选解中的违例。
