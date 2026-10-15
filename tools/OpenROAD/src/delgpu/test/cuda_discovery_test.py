# SPDX-License-Identifier: BSD-3-Clause
"""Exercise CUDA discovery without invoking a compiler or requiring CUDA."""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


BUILD_SCRIPT = Path(sys.argv.pop(1)).resolve()


class CudaDiscoveryTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="delgpu-cuda-")
        self.addCleanup(self.temp.cleanup)
        self.workspace = Path(self.temp.name)
        self.root = self.workspace / "orfs/tools/OpenROAD"
        self.script = self.root / "src/delgpu/scripts/build.sh"
        self.script.parent.mkdir(parents=True)
        shutil.copyfile(BUILD_SCRIPT, self.script)
        self.bin = self.workspace / "bin"
        self.bin.mkdir()
        # A controlled PATH keeps the test independent of the host's nvcc.
        for tool in ["bash", "dirname", "nproc", "readlink", "sed", "head"]:
            (self.bin / tool).symlink_to(shutil.which(tool))
        (self.bin / "python3").symlink_to(sys.executable)
        self.bazel = self.bin / "bazel"
        self.bazel.write_text(
            "#!/usr/bin/env python3\n"
            "import json, sys\nprint(json.dumps(sys.argv[1:]))\n"
        )
        self.bazel.chmod(0o755)
        self.env = {
            "PATH": str(self.bin),
            "HOME": str(self.workspace / "home"),
            "BAZEL": str(self.bazel),
        }
        self.local = self.toolkit(self.workspace / ".tools/cuda-12.9")

    def toolkit(self, path):
        for name in [
            "include/cuda.h",
            "include/cuda_runtime.h",
            "bin/ptxas",
            "bin/fatbinary",
            "nvvm/libdevice/libdevice.10.bc",
            "lib64/libcudart.so",
        ]:
            file = path / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.touch()
            if name.startswith("bin/"):
                file.chmod(0o755)
        return path

    def run_build(self, *args, **env):
        return subprocess.run(
            [str(self.bin / "bash"), str(self.script), *args],
            env={**self.env, **env},
            capture_output=True,
            text=True,
            check=False,
        )

    def assert_toolkit(self, result, toolkit):
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(result.stdout.splitlines()[-1])
        self.assertIn(f"--repo_env=OPENROAD_CUDA_PATH={toolkit.resolve()}", arguments)
        return arguments

    def test_workspace_toolkit_without_nvcc(self):
        self.assert_toolkit(self.run_build(), self.local)

    def test_explicit_path_precedes_other_candidates(self):
        explicit = self.toolkit(self.workspace / "custom toolkit")
        self.assert_toolkit(
            self.run_build(OPENROAD_CUDA_PATH=str(explicit), CUDA_HOME="/missing"),
            explicit,
        )

    def test_cuda_environment_aliases(self):
        explicit = self.toolkit(self.workspace / "custom")
        for variable in ["CUDA_HOME", "CUDA_PATH"]:
            with self.subTest(variable=variable):
                self.assert_toolkit(
                    self.run_build(**{variable: str(explicit)}), explicit
                )

    def test_incomplete_explicit_toolkit_fails_before_bazel(self):
        (self.local / "nvvm/libdevice/libdevice.10.bc").unlink()
        result = self.run_build(OPENROAD_CUDA_PATH=str(self.local))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing nvvm/libdevice", result.stderr)
        self.assertNotIn("//src/delgpu:libdelgpu.so", result.stdout)

    def test_nvcc_wrapper_reports_actual_prefix(self):
        explicit = self.toolkit(self.workspace / "nvcc-toolkit")
        nvcc = self.bin / "nvcc"
        nvcc.write_text(f'#!/bin/bash\nprintf "#\\$ TOP=%s\\n" "{explicit}" >&2\n')
        nvcc.chmod(0o755)
        self.assert_toolkit(self.run_build(), explicit)

    def test_runtime_only_nvcc_is_skipped(self):
        nvcc = self.bin / "nvcc"
        nvcc.write_text('#!/bin/bash\nprintf "#\\$ TOP=/missing\\n" >&2\n')
        nvcc.chmod(0o755)
        self.assert_toolkit(self.run_build(), self.local)

    def test_cpu_build_does_not_require_cuda(self):
        result = self.run_build("--cpu", OPENROAD_CUDA_PATH="/missing")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = json.loads(result.stdout)
        self.assertIn("--//src/delgpu:gpu=none", arguments)
        self.assertFalse(
            any(arg.startswith("--repo_env=OPENROAD_CUDA_PATH=") for arg in arguments)
        )


if __name__ == "__main__":
    unittest.main()
