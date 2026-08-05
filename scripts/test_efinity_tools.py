#!/usr/bin/env python3
"""Unit tests for Efinity image conversion and build-report validation."""

from __future__ import annotations

import contextlib
import io
from pathlib import Path
import tempfile
import unittest

from check_efinity_reports import (
    EXPECTED_PINS,
    verify_pinout,
    verify_sdc_coverage,
    verify_timing,
)
from efinity_hex_to_bin import BITSTREAM_HEADER_BYTES, BITSTREAM_MIN_BYTES, convert_file


def bitstream_hex(header_line: str = "Mode: passive", total_bytes: int | None = None) -> str:
    """Hex text for a synthetic image: the documented 256-byte NUL-padded ASCII
    header followed by payload padding out to a plausible bitstream size."""
    if total_bytes is None:
        total_bytes = BITSTREAM_MIN_BYTES
    header = (
        f"Version: 2026.1\nProject: test\nFamily: Trion\n{header_line}\n".encode("ascii")
    )
    image = header.ljust(BITSTREAM_HEADER_BYTES, b"\x00")
    image += b"\xa5" * max(0, total_bytes - len(image))
    return image[:total_bytes].hex()


def valid_pinout() -> str:
    return "\n".join(
        f"|     {package_pin}     | {signal} |"
        for signal, package_pin in EXPECTED_PINS.items()
    )


def timing_report(setup_slack: str = "8.136", hold_slack: str = "0.642") -> str:
    return f"""
SDC Filename: constraints/forgix_hello_world.sdc
User target constrained clocks
  clk_32m       31.250        32.000
Setup (Max) Clock Relationship
  clk_32m       clk_32m       31.250       {setup_slack}
Hold (Min) Clock Relationship
  clk_32m       clk_32m       0.000        {hold_slack}
"""


def sdc_text(ports: str = "clk_32m spi_cs_n") -> str:
    names = ports.split()
    lines = [f"create_clock -period 31.250 -name clk_32m [get_ports {{{names[0]}}}]"]
    if len(names) > 1:
        rest = " ".join(names[1:])
        lines.append(f"set_false_path -from [get_ports {{{rest}}}]")
    return "\n".join(lines) + "\n"


def interface_csv(ports: str = "clk_32m spi_cs_n") -> str:
    rows = ["# Efinity Interface Configuration"]
    rows.extend(f"input, 0, {index}, 1, __bypass__, {name}"
                for index, name in enumerate(ports.split()))
    return "\n".join(rows) + "\n"


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

    def test_accepts_a_plausible_bitstream_when_validating(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "image.hex"
            source.write_text(bitstream_hex(), encoding="ascii")

            count = convert_file(
                source, source.with_suffix(".bin"), validate_bitstream_image=True
            )

            self.assertEqual(count, BITSTREAM_MIN_BYTES)

    def test_rejects_a_truncated_bitstream(self) -> None:
        # An interrupted write or a full disk leaves a short hex that still
        # converts cleanly and passes a non-empty check.
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "short.hex"
            source.write_text(bitstream_hex(total_bytes=100), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "implausibly small"):
                convert_file(
                    source, source.with_suffix(".bin"), validate_bitstream_image=True
                )

    def test_rejects_a_header_without_the_passive_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "wrongmode.hex"
            source.write_text(bitstream_hex(header_line="Mode: jtag"), encoding="ascii")
            with self.assertRaisesRegex(ValueError, "Mode: passive"):
                convert_file(
                    source, source.with_suffix(".bin"), validate_bitstream_image=True
                )

    def test_rejects_a_non_ascii_header(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "binary.hex"
            source.write_text("a5" * BITSTREAM_MIN_BYTES, encoding="ascii")
            with self.assertRaisesRegex(ValueError, "ASCII Efinity header"):
                convert_file(
                    source, source.with_suffix(".bin"), validate_bitstream_image=True
                )

    def test_validation_is_off_by_default(self) -> None:
        # The CI fixture path converts tiny non-bitstream files on purpose;
        # plain conversion must keep accepting them.
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "tiny.hex"
            source.write_text("00 12", encoding="ascii")
            self.assertEqual(convert_file(source, source.with_suffix(".bin")), 2)


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

    def test_rejects_a_report_from_the_wrong_sdc(self) -> None:
        # STA against a generated template or a stale constraints path passes
        # every slack check while proving nothing about our constraints.
        with tempfile.TemporaryDirectory() as temporary:
            timing = self.write_report(
                Path(temporary),
                "timing.rpt",
                timing_report().replace(
                    "constraints/forgix_hello_world.sdc", "outflow/template.pt.sdc"
                ),
            )
            with self.assertRaisesRegex(ValueError, "not produced from the project SDC"):
                verify_timing(timing)

    def test_accepts_matching_sdc_and_interface(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdc = self.write_report(root, "design.sdc", sdc_text("clk_32m spi_cs_n"))
            csv = self.write_report(root, "iface.csv", interface_csv("clk_32m spi_cs_n"))
            with contextlib.redirect_stdout(io.StringIO()):
                verify_sdc_coverage(sdc, csv)

    def test_rejects_an_sdc_port_missing_from_the_design(self) -> None:
        # A renamed port makes get_ports match nothing: Efinity drops the
        # constraint without failing, so the checker has to notice instead.
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdc = self.write_report(root, "design.sdc", sdc_text("clk_32m spi_cs_renamed"))
            csv = self.write_report(root, "iface.csv", interface_csv("clk_32m spi_cs_n"))
            with self.assertRaisesRegex(ValueError, "spi_cs_renamed"):
                verify_sdc_coverage(sdc, csv)

    def test_rejects_an_interface_port_the_sdc_never_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdc = self.write_report(root, "design.sdc", sdc_text("clk_32m"))
            csv = self.write_report(root, "iface.csv", interface_csv("clk_32m button_n"))
            with self.assertRaisesRegex(ValueError, "button_n"):
                verify_sdc_coverage(sdc, csv)


if __name__ == "__main__":
    unittest.main()
