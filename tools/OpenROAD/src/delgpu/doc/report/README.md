# DELGPU 全流程汇报文档

- `delgpu_full_report.pdf`：排版后的汇报正文，含目录、形式化模型、6 组伪代码、11 张图和实测表。
- `delgpu_full_report.html`：可离线打开，图片已内嵌，公式使用原生 MathML，无外部脚本依赖。
- `delgpu_full_report.md`：可继续编辑的源稿；图片位于 `assets/`。
- `assets/`：流程/模型/性能图的 PNG 与 SVG、真实 OpenROAD GUI 截图、PDF 使用的公式图片。
- `data/`：冻结的 CSV/JSON、GUI 截图日志和源码/数据 SHA-256 清单。

按用户要求，不生成 Word 文件。

## 内容与证据

正文描述当前实现，不把计划当作已完成功能。当前默认配置为 150 轮动态学习率 + GPU
整数后处理 + cooperative 8 次回缩。真实 GUI 截图由 `capture_report.tcl` 重新执行默认
150 轮生成；截图运行与性能测量分开。质量与耗时来源、限制和代码对应关系见正文附录。

## 重新生成

在 ORFS 根目录执行，先具备当前 CUDA OpenROAD 可执行文件。仅改正文不需要重新运行 GUI。

```bash
python3 -m pip install -r tools/OpenROAD/src/delgpu/doc/report/requirements-report.txt
# 可选：重新截图，需要已编译的 GUI 宿主；算法使用当前默认配置
QT_QPA_PLATFORM=offscreen ./dev-openroad.sh run -gui -no_settings -exit \
  tools/OpenROAD/src/delgpu/doc/report/capture_report.tcl \
  > tools/OpenROAD/src/delgpu/doc/report/data/gui_capture.log 2>&1
# 生成图表，冻结已保存的测量数据；不会重跑性能实验
python3 tools/OpenROAD/src/delgpu/doc/report/generate_figures.py
# 生成 PDF、离线 HTML 和来源哈希；另需 pandoc
python3 tools/OpenROAD/src/delgpu/doc/report/build_report.py
```

中文字体默认读取当前 WSL 可访问的 Windows 微软雅黑 TTC；也可通过
`DELGPU_REPORT_CJK_FONT` 和 `DELGPU_REPORT_CJK_BOLD_FONT` 指定本地 TrueType 字体。
PDF 内嵌所用字体。`requirements-report.txt` 中 PyMuPDF 用于 PDF 导出后的完整性检查。

图 9 的建图时间来自历史同输入五次测量；其余最新回缩时间来自三次交替对照。
不能把不同实验的时间相加，或用当前 GPU 最终结果直接计算原版 CPU LP/MIP 的同输入加速比。
