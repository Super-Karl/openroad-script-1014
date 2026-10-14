#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
openroad_root=$(cd -- "$script_dir/../../.." && pwd)
backend=cuda
module_only=false
while (($#)); do
  case "$1" in
    --cpu) backend=none; shift ;;
    --module-only) module_only=true; shift ;;
    -h|--help)
      cat <<'HELP'
Usage: src/delgpu/scripts/build.sh [--cpu] [--module-only] [-- BAZEL_OPTIONS...]

Build libdelgpu.so and OpenROAD with Bazel (CUDA by default).
  --cpu          Build without CUDA; the delgpu command reports unavailable.
  --module-only  Build only libdelgpu.so, e.g. after editing module sources.
  --             Pass remaining options to bazel build (e.g. --jobs=24).

BAZEL selects the Bazel executable; otherwise use bazelisk or bazel.
CUDA uses OPENROAD_CUDA_PATH, CUDA_HOME, or the toolkit detected from nvcc.
Module CUDA does not require the optional Kokkos placement backend.
Outputs: bazel-bin/src/delgpu/libdelgpu.so and bazel-bin/openroad.
HELP
      exit 0 ;;
    --) shift; break ;;
    *) printf 'Unknown option: %s (use --help)\n' "$1" >&2; exit 2 ;;
  esac
done
if [[ -n ${BAZEL:-} ]]; then
  bazel_bin=$BAZEL
elif command -v bazelisk >/dev/null 2>&1; then
  bazel_bin=bazelisk
elif command -v bazel >/dev/null 2>&1; then
  bazel_bin=bazel
else
  printf 'Bazel not found; install bazelisk or set BAZEL.\n' >&2
  exit 1
fi
cd -- "$openroad_root"
targets=(//src/delgpu:libdelgpu.so)
if [[ $module_only == false ]]; then
  targets+=(//:openroad)
fi
cuda_options=()
if [[ $backend == cuda ]]; then
  cuda_root=${OPENROAD_CUDA_PATH:-${CUDA_HOME:-}}
  if [[ -z $cuda_root ]] && command -v nvcc >/dev/null 2>&1; then
    # nvcc may be a wrapper; TOP reports the actual toolkit directory.
    cuda_root=$(nvcc -v __delgpu_probe__.cu 2>&1 | sed -n 's/^#\$ TOP=//p' | head -n 1) || true
    if [[ -n $cuda_root && -d $cuda_root ]]; then
      cuda_root=$(cd -- "$cuda_root" && pwd)
    else
      cuda_root=$(dirname -- "$(dirname -- "$(readlink -f -- "$(command -v nvcc)")")")
    fi
  fi
  if [[ -n $cuda_root ]]; then
    cuda_options+=("--repo_env=OPENROAD_CUDA_PATH=$cuda_root")
  fi
fi
exec "$bazel_bin" build --jobs=24 "--//src/delgpu:gpu=$backend" "${cuda_options[@]}" "$@" "${targets[@]}"
