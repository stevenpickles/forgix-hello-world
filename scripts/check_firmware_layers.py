#!/usr/bin/env python3
"""Protect the dependency boundary between application code and board support."""

from __future__ import annotations

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent
SOURCE_ROOT = ROOT / "firmware" / "src"
BSP_ROOT = SOURCE_ROOT / "bsp"
APPLICATION_ROOT = SOURCE_ROOT / "application"
FORBIDDEN_OUTSIDE_BSP = ("hardware/", "pico/", "fpga_image.h")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def source_files(root: Path) -> list[Path]:
    return sorted(path for path in root.rglob("*") if path.suffix in {".c", ".h"})


def main() -> int:
    main_source = (SOURCE_ROOT / "main.c").read_text(encoding="utf-8")
    require('#include "bsp.h"' in main_source, "main.c must initialize through bsp.h")
    require('#include "application.h"' in main_source, "main.c must dispatch through application.h")
    require("BSP_Init()" in main_source, "main.c must initialize the BSP")
    require("application_init(" in main_source, "main.c must initialize the application")
    require("application_run()" in main_source, "main.c must run the application")

    for path in [SOURCE_ROOT / "main.c", *source_files(APPLICATION_ROOT)]:
        text = path.read_text(encoding="utf-8")
        for forbidden in FORBIDDEN_OUTSIDE_BSP:
            require(forbidden not in text, f"{path.relative_to(ROOT)} bypasses the BSP with {forbidden}")

    application_source = (APPLICATION_ROOT / "application.c").read_text(encoding="utf-8")
    require('#include "bsp.h"' in application_source,
            "application business logic must consume the BSP umbrella")

    umbrella = (BSP_ROOT / "bsp.h").read_text(encoding="utf-8")
    public_hardware_headers = sorted(
        path for path in BSP_ROOT.glob("bsp_*.h") if not path.name.endswith("_internal.h")
    )
    for header in public_hardware_headers:
        require(f'#include "{header.name}"' in umbrella,
                f"bsp.h must aggregate public header {header.name}")

    for header in [BSP_ROOT / "bsp.h", *public_hardware_headers]:
        text = header.read_text(encoding="utf-8")
        include_lines = "\n".join(re.findall(r"^#include .+$", text, flags=re.MULTILINE))
        for forbidden in FORBIDDEN_OUTSIDE_BSP:
            require(forbidden not in include_lines,
                    f"public BSP header {header.name} exposes {forbidden}")

    print(f"Firmware layering passed: {len(public_hardware_headers)} BSP hardware headers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
