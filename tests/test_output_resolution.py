"""Output mode policy, failure recovery, and optional real SDL/config checks."""
from pathlib import Path
import shutil
import subprocess

import pytest


def test_output_resolution(tmp_path):
    if not shutil.which("cmake") or not (shutil.which("cc") or shutil.which("clang")):
        pytest.skip("CMake and a C compiler are required")
    root = Path(__file__).resolve().parents[1]
    subprocess.run(["cmake", "-S", str(root / "tests/host/output_resolution"),
                    "-B", str(tmp_path)], check=True, timeout=60)
    subprocess.run(["cmake", "--build", str(tmp_path)], check=True, timeout=120)
    subprocess.run(["ctest", "--test-dir", str(tmp_path), "--output-on-failure"],
                   check=True, timeout=60)
