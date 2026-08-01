#!/usr/bin/env python3
"""Verify the Efinity build used the Forgix pinout and met its clock target."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


EXPECTED_PINS = {
    "clk_32m": "B4",
    "spi_cs_n": "G3",
    "spi_sck": "F3",
    "spi_sdio": "F2",
    "button_n": "G6",
    "led_r_n": "E1",
    "led_g_n": "F1",
    "led_b_n": "G1",
}


def verify_pinout(report: Path) -> None:
    lines = report.read_text(encoding="utf-8").splitlines()
    for signal, package_pin in EXPECTED_PINS.items():
        if not any(signal in line and f"|     {package_pin}" in line for line in lines):
            raise ValueError(f"{signal} is not assigned to package pin {package_pin}")


def _relationship_slack(report: str, constraint: str) -> float:
    match = re.search(
        rf"clk_32m\s+clk_32m\s+{re.escape(constraint)}\s+(-?\d+(?:\.\d+)?)",
        report,
    )
    if match is None:
        raise ValueError(f"clock relationship with {constraint} ns constraint not found")
    return float(match.group(1))


def verify_timing(report: Path) -> None:
    text = report.read_text(encoding="utf-8")
    if not re.search(r"clk_32m\s+31\.250\s+32\.000", text):
        raise ValueError("32 MHz clk_32m timing constraint was not applied")

    setup_slack = _relationship_slack(text, "31.250")
    hold_slack = _relationship_slack(text, "0.000")
    if setup_slack < 0 or hold_slack < 0:
        raise ValueError(
            f"timing failed: setup slack {setup_slack:.3f} ns, "
            f"hold slack {hold_slack:.3f} ns"
        )
    print(
        f"Timing passed: setup slack {setup_slack:.3f} ns, "
        f"hold slack {hold_slack:.3f} ns"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pinout_report", type=Path)
    parser.add_argument("timing_report", type=Path)
    args = parser.parse_args()
    verify_pinout(args.pinout_report)
    verify_timing(args.timing_report)
    print("Forgix package pin assignments passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
