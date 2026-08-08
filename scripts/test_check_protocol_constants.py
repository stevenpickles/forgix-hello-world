#!/usr/bin/env python3
"""Unit tests for check_protocol_constants.py.

Run as `python scripts/test_check_protocol_constants.py` from the repository
root, matching the other script tests. The parsers take text, so most cases
need no temporary files; the last test runs the whole check against the real
repository, which is the integration gate CI relies on.
"""

from __future__ import annotations

import unittest

from check_protocol_constants import (
    C_FILE_NAMES,
    ROOT,
    VHDL_NAMES,
    check_c_files,
    check_vhdl,
    parse_c_constants,
    parse_doc_tables,
    parse_vhdl_constants,
    run_all_checks,
)


class VhdlParsing(unittest.TestCase):
    def test_parses_constants_despite_formatting(self) -> None:
        text = (
            'constant DESIGN_ID        : byte_t := x"B7";\n'
            '  constant CMD_WRITE:byte_t   :=   x"02" ;\n'
            'constant REG_TICK_3 : byte_t := x"33";\n'
        )
        parsed = parse_vhdl_constants(text)
        self.assertEqual({"DESIGN_ID": 0xB7, "CMD_WRITE": 0x02, "REG_TICK_3": 0x33}, parsed)

    def test_ignores_non_byte_constants(self) -> None:
        text = "constant STABLE_CYCLES : positive := 100;\n"
        self.assertEqual({}, parse_vhdl_constants(text))


class CParsing(unittest.TestCase):
    def test_parses_defines_and_enum_members(self) -> None:
        text = (
            "#define REG_STATUS ( (uint8_t) 0x01 )\n"
            "#define REG_TICK_CAPTURE ( (uint8_t) 0x30 )\n"
            "    CMD_WRITE = 0x02,\n"
            "    CMD_PING = 0x9f,\n"
        )
        parsed = parse_c_constants(text)
        self.assertEqual(
            {"REG_STATUS": 0x01, "REG_TICK_CAPTURE": 0x30, "CMD_WRITE": 0x02, "CMD_PING": 0x9F},
            parsed,
        )

    def test_ignores_unrelated_defines(self) -> None:
        text = "#define PIN_CDONE ( (uint8_t) 0x05 )\n    BSP_BOOT_POWER_ON = 0x01,\n"
        self.assertEqual({}, parse_c_constants(text))


class DocParsing(unittest.TestCase):
    def test_returns_cells_for_table_rows_only(self) -> None:
        text = "prose line\n| `0x01` | STATUS | R/W | meaning |\nmore prose\n"
        rows = parse_doc_tables(text)
        self.assertEqual([["0x01", "STATUS", "R/W", "meaning"]], rows)


class VhdlChecks(unittest.TestCase):
    def test_parse_miss_fails_loudly(self) -> None:
        """A package the regex cannot read must error, not pass vacuously."""
        real = check_vhdl.__globals__["VHDL_PACKAGE"]
        try:
            check_vhdl.__globals__["VHDL_PACKAGE"] = ROOT / "docs" / "register-map.md"
            errors: list[str] = []
            check_vhdl(errors)
            self.assertTrue(any("not parsed" in error for error in errors))
        finally:
            check_vhdl.__globals__["VHDL_PACKAGE"] = real

    def test_expected_name_sets_are_consistent(self) -> None:
        """Every C-side name must exist in the VHDL authority set."""
        for names in C_FILE_NAMES.values():
            self.assertTrue(names.issubset(VHDL_NAMES), names.difference(VHDL_NAMES))

    def test_reg_id_and_features_are_intentionally_firmware_absent(self) -> None:
        c_names = set().union(*C_FILE_NAMES.values())
        self.assertNotIn("REG_ID", c_names)
        self.assertNotIn("REG_FEATURES", c_names)


class ValueComparison(unittest.TestCase):
    def test_value_mismatch_is_reported_per_name(self) -> None:
        vhdl = {name: 0x99 for name in VHDL_NAMES}
        errors: list[str] = []
        check_c_files(vhdl, errors)
        # Every real C constant disagrees with the fake authority, and each
        # mismatch names its constant.
        expected = sum(len(names) for names in C_FILE_NAMES.values())
        self.assertEqual(expected, len(errors))
        self.assertTrue(all("0x99" in error for error in errors))


class RepositoryConsistency(unittest.TestCase):
    def test_repository_is_currently_consistent(self) -> None:
        self.assertEqual([], run_all_checks())


if __name__ == "__main__":
    unittest.main()
