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
  run            启动 Tcl 终端，或传入 OpenROAD 参数运行脚本
  help           显示帮助

示例：
  ./dev-openroad.sh build
  ./dev-openroad.sh build -- --jobs=8
  ./dev-openroad.sh build-delgpu -- --jobs=8
  ./dev-openroad.sh gui
  ./dev-openroad.sh gui --db /路径/design.odb
  ./dev-openroad.sh gui --empty
  ./dev-openroad.sh run -exit tools/OpenROAD/src/delgpu/scripts/open_route.tcl

build/build-delgpu 的参数直接传给模块构建脚本；支持 --cpu。
默认使用当前全部可用 CPU 线程（nproc），可通过 -- --jobs=N 覆盖。
两种编译命令均默认启用 --//:platform=gui，保持宿主和模块配置一致。
BAZEL 指定 Bazel 程序，OPENROAD_CUDA_PATH 或 CUDA_HOME 指定 CUDA 路径。
DELGPU_ROUTE_DB 可覆盖 GUI 默认数据库；--db 优先于该环境变量。
gui 可在 -- 后传入额外 OpenROAD 参数。相对路径以调用时的目录为基准。
在 GUI 的 Tcl Console 中输入 delgpu 即可调用模块（无参数）。
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
  gui|run)
    [[ -x "$openroad_bin" ]] || fail "找不到 OpenROAD 可执行文件，请先运行 $orfs_root/dev-openroad.sh build"
    if [[ "$command_name" == run ]]; then
      exec "$openroad_bin" -no_init "$@"
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
