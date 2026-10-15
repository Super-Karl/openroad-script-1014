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
  --cpu          Build without CUDA; run delgpu -backend cpu.
  --module-only  Build only libdelgpu.so, e.g. after editing module sources.
  --             Pass remaining options to bazel build (e.g. --jobs=24).

BAZEL selects the Bazel executable; otherwise use bazelisk or bazel.
CUDA uses OPENROAD_CUDA_PATH, CUDA_HOME, CUDA_PATH, nvcc, or a toolkit
in a standard install or project-local .tools/dependencies directory.
Explicit CUDA paths must point to a complete toolkit; invalid paths fail early.
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
  cuda_toolkit_problem() {
    local prefix=$1 required
    for required in include/cuda.h include/cuda_runtime.h bin/ptxas bin/fatbinary; do
      if [[ ! -r "$prefix/$required" ]]; then
        printf 'missing %s' "$required"
        return 1
      fi
    done
    if [[ ! -x "$prefix/bin/ptxas" || ! -x "$prefix/bin/fatbinary" ]]; then
      printf 'bin/ptxas and bin/fatbinary must be executable'
      return 1
    fi
    if ! compgen -G "$prefix/nvvm/libdevice/libdevice*.bc" >/dev/null; then
      printf 'missing nvvm/libdevice/libdevice*.bc'
      return 1
    fi
    if [[ ! -r "$prefix/lib64/libcudart.so" && ! -r "$prefix/lib/libcudart.so" ]]; then
      printf 'missing lib64/libcudart.so or lib/libcudart.so'
      return 1
    fi
  }

  find_cuda_toolkit() {
    local variable candidate problem nvcc_path nvcc_prefix search_root
    for variable in OPENROAD_CUDA_PATH CUDA_HOME CUDA_PATH; do
      candidate=${!variable:-}
      if [[ -n $candidate ]]; then
        if problem=$(cuda_toolkit_problem "$candidate"); then
          (cd -- "$candidate" && pwd -P)
          return
        fi
        printf 'Invalid CUDA toolkit: %s=%s (%s).\n' "$variable" "$candidate" "$problem" >&2
        return 1
      fi
    done

    if nvcc_path=$(command -v nvcc); then
      # TOP also works when nvcc on PATH is a wrapper rather than a symlink.
      nvcc_prefix=$("$nvcc_path" -v --dryrun -x cu /dev/null -o /dev/null 2>&1 \
        | sed -n 's/^#\$ TOP=//p' | head -n 1) || true
      for candidate in "$nvcc_prefix" "$(dirname -- "$(dirname -- "$(readlink -f -- "$nvcc_path")")")"; do
        if [[ -n $candidate ]] && cuda_toolkit_problem "$candidate" >/dev/null; then
          (cd -- "$candidate" && pwd -P)
          return
        fi
      done
    fi

    # Search project, ORFS, workspace and user installs before system installs.
    # Clang CUDA needs a toolkit but does not require nvcc to be on PATH.
    for search_root in "$openroad_root" "$openroad_root/../.." "$openroad_root/../../.." \
      "${HOME:-/nonexistent}" /usr/local /opt /opt/nvidia; do
      for candidate in "$search_root"/cuda "$search_root"/cuda-* \
        "$search_root"/.tools/cuda* "$search_root"/dependencies/cuda* \
        "$search_root"/.local/opt/cuda*; do
        if cuda_toolkit_problem "$candidate" >/dev/null; then
          (cd -- "$candidate" && pwd -P)
          return
        fi
      done
    done
    printf 'No complete CUDA toolkit found. Set OPENROAD_CUDA_PATH to a toolkit with headers, ptxas, fatbinary, libdevice and libcudart, or use --cpu.\n' >&2
    return 1
  }

  if ! cuda_root=$(find_cuda_toolkit); then
    exit 1
  fi
  printf '[INFO] Using CUDA toolkit: %s\n' "$cuda_root"
  cuda_options+=("--repo_env=OPENROAD_CUDA_PATH=$cuda_root")
fi
exec "$bazel_bin" build "--jobs=$(nproc)" "--//src/delgpu:gpu=$backend" "${cuda_options[@]}" "$@" "${targets[@]}"
