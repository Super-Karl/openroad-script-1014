# delgpu：OpenROAD GPU 小模块

本模块位于 OpenROAD 的 `src/delgpu`，通过 Bazel 构建为 `libdelgpu.so`。
OpenROAD 启动时链接共享库、创建模块实例并注册 Tcl 命令：

```tcl
delgpu
```

命令检查默认 CUDA 设备（设备 0）、初始化 CUDA 上下文并同步设备，
打印设备名称、显存、计算能力、SM 数量及 CUDA 驱动/运行时版本。
随后读取当前 OpenDB block，打印设计名、实例/网络/引脚数量、布线统计、
布线层范围、DBU/微米和 die 尺寸。每次调用都重新获取当前 block。
无 GPU 或 CUDA 初始化失败时返回错误；尚未加载设计或工艺时打印提示。

## Bazel 构建

在 `OpenROAD-flow-scripts/tools/OpenROAD` 根目录执行（脚本不会运行程序）：

```bash
# CUDA 共享库及宿主
src/delgpu/scripts/build.sh -- --jobs=24

# 修改模块后，只构建共享库
src/delgpu/scripts/build.sh --module-only -- --jobs=24

# 无 CUDA 的版本，用于接入检查；delgpu 命令会提示 CUDA 未启用
src/delgpu/scripts/build.sh --cpu -- --jobs=24
```

脚本优先使用 `bazelisk`，其次使用 `bazel`，可通过 `BAZEL` 指定可执行文件。
等价的直接命令：

```bash
bazelisk build --//src/delgpu:gpu=cuda //src/delgpu:libdelgpu.so //:openroad
bazelisk build --//src/delgpu:gpu=none //src/delgpu:libdelgpu.so //:openroad
```

输出：

- `bazel-bin/src/delgpu/libdelgpu.so`：模块共享库。
- `bazel-bin/openroad`：已接入模块的宿主。

CUDA 使用仓库既有的 `@cuda_local`。脚本优先读取 `OPENROAD_CUDA_PATH`
或 `CUDA_HOME`，否则从 `nvcc` 自动检测本机工具包路径。
当前模块仅调用 CUDA Runtime，无设备 kernel，因此使用普通 C++ 编译。
模块开关 `--//src/delgpu:gpu=cuda` 与宿主的可选 Kokkos 后端独立，
测试本模块无需安装 Kokkos/KokkosFFT。
直接调用 Bazel 时可传入
`--repo_env=OPENROAD_CUDA_PATH=/实际/CUDA/路径`。
脚本默认使用 24 个并行任务，可用额外的 `--jobs` 覆盖。
宿主与共享库需使用相同的构建选项；首次接入需要一起构建。

共享库仅包含模块实现及所选 GPU 后端，OpenDB、日志、Tcl 和 OpenROAD
接口由宿主提供。`host_headers.bzl` 保留这些依赖的编译信息，避免把宿主的
静态库再次打进模块。宿主导出符号并通过 `cc_import` 链接共享库；
Bazel 管理运行时库路径。此共享库配置面向 Linux/ELF，不能独立运行，
也不能直接加载到未接入模块的 OpenROAD 可执行文件。

## 加载设计

```bash
bazel-bin/openroad -no_splash -no_init -exit src/delgpu/scripts/open_route.tcl
```

默认读取当前 ORFS 的 `flow/results/asap7/ibex/base/5_2_route.odb`，
可用 `DELGPU_ROUTE_DB` 指定其他 ODB：

```bash
DELGPU_ROUTE_DB=/路径/design.odb bazel-bin/openroad \
  -no_splash -no_init -exit src/delgpu/scripts/open_route.tcl
```

也可在控制台执行 `read_db /路径/design.odb` 后调用 `delgpu`。
当前阶段仅检查 GPU 并读取数据库，后续实现见 [计划](doc/plan.md)。
原有 CMake 配置保留；本页及构建脚本使用 Bazel。
