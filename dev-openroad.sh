#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
set -euo pipefail

orfs_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
openroad_root="$orfs_root/tools/OpenROAD"
build_script="$openroad_root/src/delgpu/scripts/build.sh"
openroad_bin="$openroad_root/bazel-bin/openroad"
default_db="$orfs_root/flow/results/asap7/ibex/base/5_2_route.odb"

usage() {
  cat <<'HELP'
用法：./dev-openroad.sh <命令> [选项]

命令：
  build          编译 CUDA delgpu 共享库和支持 GUI 的 OpenROAD 主程序
  build-delgpu   只编译 delgpu 共享库（首次使用请先 build）
  gui            启动 GUI，默认加载 asap7/ibex/base/5_2_route.odb
  graph          加载设计并建图，默认无界面；--gui 1 显示 trim 和边
  optimize       建图并迭代优化，默认 150 轮、0.25W 步长、35nm 跳跃、动态学习率 1.5→0.5、无 GUI
  run            启动 Tcl 终端，或传入 OpenROAD 参数运行脚本
  help           显示帮助

示例：
  ./dev-openroad.sh build
  ./dev-openroad.sh build -- --jobs=8
  ./dev-openroad.sh build-delgpu -- --jobs=8
  ./dev-openroad.sh gui
  ./dev-openroad.sh gui --db /路径/design.odb
  ./dev-openroad.sh gui --empty
  ./dev-openroad.sh graph --gui 1 --verify 1
  ./dev-openroad.sh graph --gui 0
  ./dev-openroad.sh graph --gui 0 --backend cpu
  ./dev-openroad.sh optimize --report /tmp/delgpu.csv
  ./dev-openroad.sh optimize --gui 1 --frame-ms 100
  ./dev-openroad.sh run -exit tools/OpenROAD/src/delgpu/scripts/open_route.tcl

build/build-delgpu 的参数直接传给模块构建脚本；支持 --cpu。
默认使用当前全部可用 CPU 线程（nproc），可通过 -- --jobs=N 覆盖。
两种编译命令均默认启用 --//:platform=gui，保持宿主和模块配置一致。
BAZEL 指定 Bazel 程序，OPENROAD_CUDA_PATH 或 CUDA_HOME 指定 CUDA 路径。
DELGPU_ROUTE_DB 可覆盖 GUI 默认数据库；--db 优先于该环境变量。
gui 可在 -- 后传入额外 OpenROAD 参数。相对路径以调用时的目录为基准。
graph 支持 --db 路径、--gui 0|1、--verify 0|1、--backend gpu|cpu。
optimize 还支持 --iterations 轮数、--max-step 线宽倍数、--jump-offset-nm 跳跃偏移、--learning-rate-mode linear|adaptive（默认 adaptive）、--learning-rate 学习率比例（默认 1.5）、--learning-rate-end 末轮比例/动态下限（默认 0.5）、--max-backtracks 回溯重试次数（默认 0）、--postprocess 0|1（GPU 默认开启）、--post-sweeps 修复扫描上限（默认 24）、--post-pair-rounds 双点轮数上限（默认 8）、--post-retract-sweeps 回缩扫描上限（默认 8）、--frame-ms 刷新间隔、--report CSV。
在 GUI 的 Tcl Console 中输入 delgpu -gui 1 即可显示图，-gui 0 关闭显示。
使用 -verify 1 做 CPU/GPU 一致性校验；性能实验默认不重复运行 CPU 校验。
修改模块并编译后，请关闭旧 GUI，再启动新进程。
HELP
}

fail() {
  printf '错误：%s\n' "$*" >&2
  exit 1
}

command_name=${1:-help}
if (($#)); then shift; fi
case "$command_name" in
  help|-h|--help)
    usage
    ;;
  build|build-delgpu)
    [[ -f "$build_script" ]] || fail "找不到模块构建脚本：$build_script"
    build_args=()
    if [[ "$command_name" == build-delgpu ]]; then
      build_args+=(--module-only)
    fi
    while (($#)) && [[ "$1" != -- ]]; do
      build_args+=("$1")
      shift
    done
    if (($#)); then shift; fi
    build_jobs=$(nproc)
    exec bash "$build_script" "${build_args[@]}" -- --//:platform=gui "--jobs=$build_jobs" "$@"
    ;;
  gui|run|graph|optimize)
    [[ -x "$openroad_bin" ]] || fail "找不到 OpenROAD 可执行文件，请先运行 $orfs_root/dev-openroad.sh build"
    if [[ "$command_name" == run ]]; then
      exec "$openroad_bin" -no_init "$@"
    fi
    if [[ "$command_name" == graph || "$command_name" == optimize ]]; then
      graph_db=${DELGPU_ROUTE_DB:-$default_db}
      graph_gui=0
      graph_verify=0
      graph_backend=gpu
      graph_iterations=0
      if [[ "$command_name" == optimize ]]; then graph_iterations=150; fi
      graph_max_step=0.25
      graph_jump_offset_nm=35
      graph_learning_rate_mode=
      graph_learning_rate_end=
      graph_learning_rate=
      graph_max_backtracks=
      graph_postprocess=
      graph_post_sweeps=
      graph_post_pair_rounds=
      graph_post_retract_sweeps=
      graph_frame_ms=100
      graph_report=
      while (($#)); do
        case "$1" in
          --db|--gui|--verify|--backend|--iterations|--max-step|--jump-offset-nm|--max-backtracks|--learning-rate|--learning-rate-end|--learning-rate-mode|--postprocess|--post-sweeps|--post-pair-rounds|--post-retract-sweeps|--frame-ms|--report)
            (($# >= 2)) || fail "$1 后需要提供值"
            case "$1" in
              --db) graph_db=$2 ;;
              --gui) graph_gui=$2 ;;
              --verify) graph_verify=$2 ;;
              --backend) graph_backend=$2 ;;
              --iterations) graph_iterations=$2 ;;
              --max-step) graph_max_step=$2 ;;
              --jump-offset-nm) graph_jump_offset_nm=$2 ;;
              --max-backtracks) graph_max_backtracks=$2 ;;
              --learning-rate) graph_learning_rate=$2 ;;
              --learning-rate-end) graph_learning_rate_end=$2 ;;
              --learning-rate-mode) graph_learning_rate_mode=$2 ;;
              --postprocess) graph_postprocess=$2 ;;
              --post-sweeps) graph_post_sweeps=$2 ;;
              --post-pair-rounds) graph_post_pair_rounds=$2 ;;
              --post-retract-sweeps) graph_post_retract_sweeps=$2 ;;
              --frame-ms) graph_frame_ms=$2 ;;
              --report) graph_report=$2 ;;
            esac
            shift 2
            ;;
          -h|--help) usage; exit 0 ;;
          *) fail "未知 $command_name 选项：$1" ;;
        esac
      done
      [[ "$graph_gui" == 0 || "$graph_gui" == 1 ]] || fail "--gui 必须为 0 或 1"
      [[ "$graph_verify" == 0 || "$graph_verify" == 1 ]] || fail "--verify 必须为 0 或 1"
      [[ "$graph_backend" == gpu || "$graph_backend" == cpu ]] || fail "--backend 必须为 gpu 或 cpu"
      [[ "$graph_verify" != 1 || "$graph_backend" == gpu ]] || fail "--verify 1 需要 --backend gpu"
      [[ -f "$graph_db" ]] || fail "找不到 ODB：$graph_db"
      export DELGPU_ROUTE_DB="$graph_db" DELGPU_GUI="$graph_gui"
      export DELGPU_VERIFY="$graph_verify" DELGPU_BACKEND="$graph_backend"
      export DELGPU_ITERATIONS="$graph_iterations" DELGPU_MAX_STEP="$graph_max_step"
      export DELGPU_JUMP_OFFSET_NM="$graph_jump_offset_nm"
      if [[ -n "$graph_max_backtracks" ]]; then
        export DELGPU_MAX_BACKTRACKS="$graph_max_backtracks"
      fi
      if [[ -n "$graph_learning_rate" ]]; then
        export DELGPU_LEARNING_RATE="$graph_learning_rate"
      fi
      if [[ -n "$graph_learning_rate_end" ]]; then
        export DELGPU_LEARNING_RATE_END="$graph_learning_rate_end"
      fi
      if [[ -n "$graph_learning_rate_mode" ]]; then
        export DELGPU_LEARNING_RATE_MODE="$graph_learning_rate_mode"
      fi
      if [[ -n "$graph_postprocess" ]]; then export DELGPU_POSTPROCESS="$graph_postprocess"; fi
      if [[ -n "$graph_post_sweeps" ]]; then export DELGPU_POST_SWEEPS="$graph_post_sweeps"; fi
      if [[ -n "$graph_post_pair_rounds" ]]; then export DELGPU_POST_PAIR_ROUNDS="$graph_post_pair_rounds"; fi
      if [[ -n "$graph_post_retract_sweeps" ]]; then export DELGPU_POST_RETRACT_SWEEPS="$graph_post_retract_sweeps"; fi
      export DELGPU_FRAME_MS="$graph_frame_ms" DELGPU_REPORT="$graph_report"
      launch_args=(-no_init -no_splash)
      if [[ "$graph_gui" == 1 ]]; then launch_args+=(-gui); else launch_args+=(-exit); fi
      exec "$openroad_bin" "${launch_args[@]}" "$openroad_root/src/delgpu/scripts/open_route.tcl"
    fi
    route_db=${DELGPU_ROUTE_DB:-$default_db}
    gui_args=()
    while (($#)); do
      case "$1" in
        --db)
          (($# >= 2)) || fail "--db 后需要提供 ODB 路径"
          [[ -n "$2" ]] || fail "ODB 路径不能为空"
          route_db=$2
          shift 2
          ;;
        --empty)
          route_db=
          shift
          ;;
        -h|--help)
          usage
          exit 0
          ;;
        --)
          shift
          gui_args+=("$@")
          break
          ;;
        *) fail "未知 GUI 选项：$1（额外 OpenROAD 参数请放在 -- 后）" ;;
      esac
    done
    db_args=()
    if [[ -n "$route_db" ]]; then
      [[ -f "$route_db" ]] || fail "找不到 ODB：$route_db；可用 --db 指定其他文件，或 --empty 打开空 GUI"
      db_args=(-db "$route_db")
    fi
    exec "$openroad_bin" -gui -no_init "${db_args[@]}" "${gui_args[@]}"
    ;;
  *)
    usage >&2
    fail "未知命令：$command_name"
    ;;
esac
