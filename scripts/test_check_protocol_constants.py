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
    check_register_map_text,
    check_vhdl,
    parse_c_constants,
    parse_doc_tables,
    parse_vhdl_constants,
    run_all_checks,
)


# A faithful miniature of docs/register-map.md's two tables, with the two
# authoritative design-ID cells parameterised so each test can mutate one.
DOC_TEMPLATE = """\
| Command | Value | Transaction |
| --- | ---: | --- |
| Write | `0x02` | `02 addr data` |
| Read | `0x03` | `03 addr`, then one byte from FPGA |
| Reset | `0x7f` | Restore defaults |
| Ping | `0x9f` | {ping_cell} |

| Address | Register | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | ID | R | {id_cell} |
| `0x01` | STATUS | R/W | status bits |
| `0x02` | FEATURES | R | bit 0 LED, bit 1 button |
| `0x10..0x12` | LED R/G/B | R/W | PWM intensity |
| `0x13` | LED GLOBAL | R/W | global brightness |
| `0x14` | LED ENABLE | R/W | bit 0 enables output |
| `0x20` | BUTTON LEVEL | R | level |
| `0x21` | BUTTON COUNT | R/W | count |
| `0x30` | TICK CAPTURE | W | capture |
| `0x30..0x33` | TICK 0..3 | R | snapshot |
"""

FAKE_VHDL = {
    "DESIGN_ID": 0xB7,
    "CMD_WRITE": 0x02,
    "CMD_READ": 0x03,
    "CMD_RESET": 0x7F,
    "CMD_PING": 0x9F,
    "REG_ID": 0x00,
    "REG_STATUS": 0x01,
    "REG_FEATURES": 0x02,
    "REG_LED_R": 0x10,
    "REG_LED_G": 0x11,
    "REG_LED_B": 0x12,
    "REG_LED_GLOBAL": 0x13,
    "REG_LED_ENABLE": 0x14,
    "REG_BUTTON": 0x20,
    "REG_BUTTON_COUNT": 0x21,
    "REG_TICK_CAPTURE": 0x30,
    "REG_TICK_0": 0x30,
    "REG_TICK_1": 0x31,
    "REG_TICK_2": 0x32,
    "REG_TICK_3": 0x33,
}


def doc_with(ping_cell: str = "Return design ID `0xb7`", id_cell: str = "`0xb7`") -> str:
    return DOC_TEMPLATE.format(ping_cell=ping_cell, id_cell=id_cell)


def register_map_errors(text: str) -> list[str]:
    errors: list[str] = []
    check_register_map_text(text, FAKE_VHDL, errors)
    return errors


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


class DesignIdRows(unittest.TestCase):
    def test_a_consistent_document_passes(self) -> None:
        self.assertEqual([], register_map_errors(doc_with()))

    def test_a_stale_ping_row_fails_on_its_own(self) -> None:
        errors = register_map_errors(doc_with(ping_cell="Return design ID `0xb6`"))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("Ping row says the design ID is 0xb6", errors[0])

    def test_a_stale_id_register_row_fails_on_its_own(self) -> None:
        errors = register_map_errors(doc_with(id_cell="`0xb6`"))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("ID row says the design ID is 0xb6", errors[0])

    def test_scattered_mentions_do_not_satisfy_the_check(self) -> None:
        """The old implementation counted matching mentions anywhere in the
        file, so prose containing the current ID twice hid two stale rows."""
        text = doc_with(ping_cell="Return design ID `0xb6`", id_cell="`0xb6`")
        text += "\nThe previous revisions answered `0xb7` and `0xb7` respectively.\n"
        errors = register_map_errors(text)
        self.assertEqual(2, len(errors), errors)
        self.assertTrue(any("Ping row" in error for error in errors))
        self.assertTrue(any("ID row" in error for error in errors))

    def test_a_ping_row_without_a_parseable_design_id_fails(self) -> None:
        errors = register_map_errors(doc_with(ping_cell="Return the identity byte"))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("must state 'design ID `0x..`' exactly once", errors[0])

    def test_a_ping_row_stating_the_id_twice_fails(self) -> None:
        errors = register_map_errors(
            doc_with(ping_cell="Return design ID `0xb7` or design ID `0xb7`")
        )
        self.assertEqual(1, len(errors), errors)
        self.assertIn("exactly once", errors[0])

    def test_an_id_row_without_a_bare_value_fails(self) -> None:
        errors = register_map_errors(doc_with(id_cell="the design ID"))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("must be the bare design-ID value", errors[0])


class RepositoryConsistency(unittest.TestCase):
    def test_repository_is_currently_consistent(self) -> None:
        self.assertEqual([], run_all_checks())


if __name__ == "__main__":
    unittest.main()
