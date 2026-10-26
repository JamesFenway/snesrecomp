"""Exercise real mod catalog selection, including exact and missing pins."""
from pathlib import Path
import shutil
import subprocess

import pytest


def test_mod_version_selection(tmp_path):
    compiler = shutil.which("g++") or shutil.which("clang++")
    if not compiler:
        pytest.skip("a C++17 compiler is required")
    root = Path(__file__).resolve().parents[1]
    sources = ["tests/mod_resources/version_selection_test.cpp",
               "runner/src/mod_runtime.cpp", "runner/src/sha256.c",
               "runner/src/crc32.c", "runner/src/snes/tier2_capture.c"]
    executable = tmp_path / "versions"
    subprocess.run([compiler, "-std=c++17", "-O1", "-I", str(root / "runner/src"),
                    *[str(root / source) for source in sources], "-o", str(executable)],
                   check=True, timeout=120)
    subprocess.run([str(executable), str(tmp_path / "catalog")], check=True, timeout=30)
