#!/usr/bin/env python3
"""Verify the Efinity build used the Forgix pinout, met its clock target, and
ran timing against the project SDC with every interface port constrained."""

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


def _sdc_ports(sdc: Path) -> set[str]:
    """Every port named inside a [get_ports {...}] list in the SDC."""
    text = sdc.read_text(encoding="utf-8")
    ports: set[str] = set()
    for group in re.findall(r"\[get_ports\s+\{([^}]*)\}\]", text):
        ports.update(group.split())
    return ports


def verify_sdc_coverage(sdc: Path, interface_csv: Path) -> None:
    # Efinity's timing report has no per-port section, and a get_ports that
    # matches nothing does not fail the build -- the constraint just
    # evaporates, the path drops out of analysis, and the slack gate stays
    # green. So the coverage check runs on the inputs instead: every port the
    # SDC names must exist in the compiled interface, and every pad in the
    # interface must be named by the SDC, so a renamed or newly added pad
    # cannot ship unconstrained-and-silent.
    interface_text = interface_csv.read_text(encoding="utf-8")
    interface_ports = {
        line.split(",")[-1].strip()
        for line in interface_text.splitlines()
        if line.strip() and not line.startswith("#")
    }
    constrained = _sdc_ports(sdc)

    missing_from_design = constrained - interface_ports
    if missing_from_design:
        raise ValueError(
            "SDC constrains ports absent from the compiled interface "
            f"(constraint silently matched nothing): {sorted(missing_from_design)}"
        )
    unconstrained = interface_ports - constrained
    if unconstrained:
        raise ValueError(
            f"interface ports carry no SDC constraint at all: {sorted(unconstrained)}"
        )
    print(f"SDC coverage passed: {len(constrained)} ports constrained")


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
    # The analysis is only as good as the constraints it ran against, so first
    # prove it ran against ours and not a generated template or a stale path.
    if not re.search(r"SDC Filename:\s+\S*forgix_hello_world\.sdc", text):
        raise ValueError("timing report was not produced from the project SDC")
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
    parser.add_argument("sdc", type=Path)
    parser.add_argument("interface_csv", type=Path)
    args = parser.parse_args()
    verify_pinout(args.pinout_report)
    verify_timing(args.timing_report)
    verify_sdc_coverage(args.sdc, args.interface_csv)
    print("Forgix package pin assignments passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
