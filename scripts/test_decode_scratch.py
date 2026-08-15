#!/usr/bin/env python3
"""Unit tests for decode_scratch.py.

Run as `python scripts/test_decode_scratch.py` from the repository root,
matching the other script tests. The health unpacking must mirror the packing
in application_diagnostics.c's snapshot slot 2 -- these tests pin the decoder's
half of that contract, since nothing else exercises this tool before it is
needed on a frozen board.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import unittest

from decode_scratch import decode_health, describe_uptime, main, word


class WordParsing(unittest.TestCase):
    def test_accepts_hex_and_decimal(self) -> None:
        self.assertEqual(0x48000000, word("0x48000000"))
        self.assertEqual(1234, word("1234"))
        self.assertEqual(0xFFFFFFFF, word("0xffffffff"))

    def test_rejects_values_outside_a_32_bit_word(self) -> None:
        for text in ("0x100000000", "-1"):
            with self.assertRaises(argparse.ArgumentTypeError):
                word(text)


class UptimeDescription(unittest.TestCase):
    def test_zero_names_the_pre_sample_freeze(self) -> None:
        self.assertIn("froze before the first one-second sample", describe_uptime(0))

    def test_nonzero_reports_minutes_and_seconds(self) -> None:
        self.assertEqual("125 s (2 min 5 s) of foreground progress", describe_uptime(125))


class HealthDecoding(unittest.TestCase):
    def test_unpacks_every_field_from_a_crafted_word(self) -> None:
        # frame 0x01c2, connected + write-blocked set, suspended clear,
        # 5 failures, 3 reconfigures -- one distinct value per field.
        health = 0x01C2 | (1 << 16) | (1 << 18) | (5 << 19) | (3 << 26)
        self.assertEqual(
            {
                "frame": 0x01C2,
                "connected": True,
                "suspended": False,
                "write_blocked": True,
                "fpga_failures": 5,
                "fpga_reconfigures": 3,
            },
            decode_health(health),
        )

    def test_fields_saturate_at_their_packed_widths(self) -> None:
        fields = decode_health(0xFFFFFFFF)
        self.assertEqual(0xFFFF, fields["frame"])
        self.assertEqual(0x7F, fields["fpga_failures"])
        self.assertEqual(0x3F, fields["fpga_reconfigures"])


class EndToEnd(unittest.TestCase):
    def run_tool(self, *argv: str) -> str:
        output = io.StringIO()
        real_argv = sys.argv
        sys.argv = ["decode_scratch.py", *argv]
        try:
            with contextlib.redirect_stdout(output):
                self.assertEqual(0, main())
        finally:
            sys.argv = real_argv
        return output.getvalue()

    def test_decodes_the_documented_example(self) -> None:
        output = self.run_tool("0x6", "0x1c2", "0x0", "0x48000000")
        self.assertIn("FPGA_CHECK - CDONE, design-ID ping, LED register readback", output)
        self.assertIn("450 s", output)
        self.assertIn("the foreground was inside the FPGA health check", output)
        # 0x48000000 >> 26 is 18 reconfigures: the reconfigured reading applies.
        self.assertIn("mode 2 attributed", output)

    def test_a_clean_board_reads_as_never_failed(self) -> None:
        output = self.run_tool("0x1", "60", "0", "0", "--ctrl", "0x40000000")
        self.assertIn("The FPGA check never failed", output)
        self.assertIn("Marker LOOP", output)
        self.assertIn("armed", output)

    def test_failures_without_recovery_point_away_from_configuration(self) -> None:
        output = self.run_tool("0x1", "60", "0", hex(5 << 19))
        self.assertIn("reconfiguration never succeeded", output)

    def test_names_the_runtime_and_boot_psram_markers(self) -> None:
        runtime = self.run_tool("10", "0", "0", "0")
        boot = self.run_tool("0x100", "0", "0", "0")
        self.assertIn("MEMTEST - running a mapped-QPI PSRAM slice", runtime)
        self.assertIn("PSRAM_POST - running the boot-only direct-mode PSRAM test", boot)


if __name__ == "__main__":
    unittest.main()
