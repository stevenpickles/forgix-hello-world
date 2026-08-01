#!/usr/bin/env python3
"""Regression checks for deterministic FPGA image embedding."""

from __future__ import annotations

import filecmp
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent.parent
EMBED = ROOT / "scripts" / "embed_image.py"
FIXTURE = ROOT / "tests" / "fixtures" / "fpga-test.bin"


def run_embed(source: Path, output_dir: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(EMBED),
            str(source),
            str(output_dir / "fpga_image.c"),
            str(output_dir / "fpga_image.h"),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def find_c_compiler() -> str:
    configured = os.environ.get("CC")
    candidates = [configured] if configured else []
    candidates.extend(["cc", "gcc", "arm-none-eabi-gcc"])
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise RuntimeError("No C compiler found; set CC to a C11 compiler")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="forgix-embed-") as temp:
        temp_dir = Path(temp)
        first = temp_dir / "first"
        second = temp_dir / "second"
        first.mkdir()
        second.mkdir()

        for output in (first, second):
            result = run_embed(FIXTURE, output)
            if result.returncode:
                raise RuntimeError(result.stderr.strip())

        for filename in ("fpga_image.c", "fpga_image.h"):
            if not filecmp.cmp(first / filename, second / filename, shallow=False):
                raise AssertionError(f"nondeterministic output: {filename}")

        compiler = find_c_compiler()
        subprocess.run(
            [
                compiler,
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{first}",
                "-c",
                str(first / "fpga_image.c"),
                "-o",
                str(first / "fpga_image.o"),
            ],
            check=True,
        )

        empty = temp_dir / "empty.bin"
        empty.touch()
        rejected = run_embed(empty, temp_dir)
        if rejected.returncode == 0 or "FPGA image is empty" not in rejected.stderr:
            raise AssertionError("empty FPGA image was not rejected clearly")

    print("image embedding checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

