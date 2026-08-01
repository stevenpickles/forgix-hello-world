#!/usr/bin/env python3
"""Compile and run the application against a host-side fake BSP."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build" / "application-tests"
APPLICATION_DIR = ROOT / "firmware" / "src" / "application"
BSP_DIR = ROOT / "firmware" / "src" / "bsp"
TEST_SOURCE = ROOT / "firmware" / "tests" / "test_application.c"
APPLICATION_SOURCE = APPLICATION_DIR / "application.c"


def find_compiler() -> str:
    requested = os.environ.get("CC")
    candidates = [requested] if requested else []
    candidates.extend(["cc", "gcc", "clang", "cl"])
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise RuntimeError("A host C compiler is required (set CC to cc, gcc, clang, or cl)")


def compile_test(compiler: str, executable: Path) -> None:
    compiler_name = Path(compiler).name.lower()
    if compiler_name in {"cl", "cl.exe"}:
        command = [
            compiler,
            "/nologo",
            "/std:c11",
            "/W4",
            "/WX",
            "/D_CRT_SECURE_NO_WARNINGS",
            f"/I{APPLICATION_DIR}",
            f"/I{BSP_DIR}",
            str(APPLICATION_SOURCE),
            str(TEST_SOURCE),
            f"/Fe:{executable}",
        ]
    else:
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-pedantic",
            f"-I{APPLICATION_DIR}",
            f"-I{BSP_DIR}",
            str(APPLICATION_SOURCE),
            str(TEST_SOURCE),
            "-o",
            str(executable),
        ]
    subprocess.run(command, cwd=BUILD_DIR, check=True)


def main() -> int:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    executable = BUILD_DIR / ("test_application.exe" if os.name == "nt" else "test_application")
    compile_test(find_compiler(), executable)
    subprocess.run([executable], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
