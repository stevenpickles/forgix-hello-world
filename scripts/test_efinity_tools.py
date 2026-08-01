#!/usr/bin/env python3
"""Unit tests for Efinity image conversion and build-report validation."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from check_efinity_reports import EXPECTED_PINS, verify_pinout, verify_timing
from efinity_hex_to_bin import convert_file


def valid_pinout() -> str:
    return "\n".join(
        f"|     {package_pin}     | {signal} |"
        for signal, package_pin in EXPECTED_PINS.items()
    )


def timing_report(setup_slack: str = "8.136", hold_slack: str = "0.642") -> str:
    return f"""
User target constrained clocks
  clk_32m       31.250        32.000
Setup (Max) Clock Relationship
  clk_32m       clk_32m       31.250       {setup_slack}
Hold (Min) Clock Relationship
  clk_32m       clk_32m       0.000        {hold_slack}
"""


class HexConversionTests(unittest.TestCase):
    def test_converts_whitespace_separated_hex(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "image.hex"
            destination = root / "nested" / "image.bin"
            source.write_text("00 12\nA5\tff\r\n", encoding="ascii")

            count = convert_file(source, destination)

            self.assertEqual(count, 4)
            self.assertEqual(destination.read_bytes(), bytes((0x00, 0x12, 0xA5, 0xFF)))

    def test_rejects_empty_image(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "empty.hex"
            source.write_text(" \r\n\t", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "Efinity image is empty"):
                convert_file(source, source.with_suffix(".bin"))

    def test_rejects_odd_length_image(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "odd.hex"
            source.write_text("123", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "invalid Efinity hex image"):
                convert_file(source, source.with_suffix(".bin"))

    def test_rejects_non_hex_characters(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "invalid.hex"
            source.write_text("00xz", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "invalid Efinity hex image"):
                convert_file(source, source.with_suffix(".bin"))


class ReportValidationTests(unittest.TestCase):
    def write_report(self, root: Path, name: str, contents: str) -> Path:
        report = root / name
        report.write_text(contents, encoding="utf-8")
        return report

    def test_accepts_expected_pinout_and_timing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pinout = self.write_report(root, "pinout.rpt", valid_pinout())
            timing = self.write_report(root, "timing.rpt", timing_report())
            with contextlib.redirect_stdout(io.StringIO()):
                verify_pinout(pinout)
                verify_timing(timing)

    def test_rejects_wrong_pin_assignment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pinout = self.write_report(
                root, "pinout.rpt", valid_pinout().replace("|     B4", "|     A1")
            )
            with self.assertRaisesRegex(ValueError, "clk_32m is not assigned"):
                verify_pinout(pinout)

    def test_rejects_missing_clock_constraint(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timing = self.write_report(
                root, "timing.rpt", timing_report().replace("32.000", "31.000")
            )
            with self.assertRaisesRegex(ValueError, "timing constraint was not applied"):
                verify_timing(timing)

    def test_rejects_negative_setup_slack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            timing = self.write_report(
                Path(temporary), "timing.rpt", timing_report(setup_slack="-0.125")
            )
            with self.assertRaisesRegex(ValueError, "timing failed"):
                verify_timing(timing)

    def test_rejects_negative_hold_slack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            timing = self.write_report(
                Path(temporary), "timing.rpt", timing_report(hold_slack="-0.010")
            )
            with self.assertRaisesRegex(ValueError, "timing failed"):
                verify_timing(timing)


if __name__ == "__main__":
    unittest.main()
