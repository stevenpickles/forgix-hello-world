#!/usr/bin/env python3
"""Verify every copy of the MCU-FPGA wire protocol agrees with forgix_pkg.vhd.

The protocol constants live in four places that are only kept in step by hand:
the VHDL package (the authority), three BSP sources plus the design-ID define in
bsp_fpga.h, the register-map document, and the hardware-test script's expected
strings. This check makes that hand-sync machine-enforced.

Parsing is strict on purpose: every expected name must actually be found, so a
formatting change that breaks a regex fails loudly instead of letting the check
pass vacuously. (The Ceedling tests' expected id= strings need no rule here --
they are compared against output printed from BSP_FPGA_DESIGN_ID, so the test
suite itself fails when the header and the strings disagree.)
"""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent

VHDL_PACKAGE = ROOT / "fpga" / "rtl" / "forgix_pkg.vhd"
BSP_FPGA_HEADER = ROOT / "firmware" / "src" / "bsp" / "bsp_fpga.h"
REGISTER_MAP = ROOT / "docs" / "register-map.md"
IBIT_DOC = ROOT / "docs" / "ibit.md"
HARDWARE_SCRIPT = ROOT / "scripts" / "test_hardware.ps1"

# The complete name set forgix_pkg.vhd must define. A parse that returns
# anything else -- fewer names or extra byte_t constants -- is an error: either
# the package changed and this table must follow, or the regex rotted.
VHDL_NAMES = frozenset(
    {
        "DESIGN_ID",
        "CMD_WRITE",
        "CMD_READ",
        "CMD_RESET",
        "CMD_PING",
        "REG_ID",
        "REG_STATUS",
        "REG_FEATURES",
        "REG_LED_R",
        "REG_LED_G",
        "REG_LED_B",
        "REG_LED_GLOBAL",
        "REG_LED_ENABLE",
        "REG_GPO",
        "REG_BUTTON",
        "REG_BUTTON_COUNT",
        "REG_TICK_CAPTURE",
        "REG_TICK_0",
        "REG_TICK_1",
        "REG_TICK_2",
        "REG_TICK_3",
    }
)

# Which protocol names each BSP source owns. REG_ID and REG_FEATURES are
# intentionally absent everywhere: the firmware reads the ID via CMD_PING and
# uses no feature bits. The C names happen to match the VHDL names today; the
# mapping is explicit so a future rename fails with a named diagnostic.
C_FILE_NAMES = {
    "firmware/src/bsp/bsp_fpga.c": frozenset(
        {
            "REG_STATUS",
            "REG_GPO",
            "REG_TICK_CAPTURE",
            "REG_TICK_0",
            "REG_TICK_1",
            "REG_TICK_2",
            "REG_TICK_3",
            "CMD_WRITE",
            "CMD_READ",
            "CMD_RESET",
            "CMD_PING",
        }
    ),
    "firmware/src/bsp/bsp_button.c": frozenset({"REG_BUTTON", "REG_BUTTON_COUNT"}),
    "firmware/src/bsp/bsp_led.c": frozenset(
        {"REG_LED_R", "REG_LED_G", "REG_LED_B", "REG_LED_GLOBAL", "REG_LED_ENABLE"}
    ),
}
C_NAME_TO_VHDL = {name: name for names in C_FILE_NAMES.values() for name in names}

# How the register-map document's row labels map onto VHDL names. A row whose
# address cell is a range expands to consecutive addresses checked against the
# listed names in order. Every label must appear exactly once.
DOC_COMMANDS = {
    "Write": "CMD_WRITE",
    "Read": "CMD_READ",
    "Reset": "CMD_RESET",
    "Ping": "CMD_PING",
}
DOC_REGISTERS = {
    "ID": ("REG_ID",),
    "STATUS": ("REG_STATUS",),
    "FEATURES": ("REG_FEATURES",),
    "LED R/G/B": ("REG_LED_R", "REG_LED_G", "REG_LED_B"),
    "LED GLOBAL": ("REG_LED_GLOBAL",),
    "LED ENABLE": ("REG_LED_ENABLE",),
    "GPO0": ("REG_GPO",),
    "BUTTON LEVEL": ("REG_BUTTON",),
    "BUTTON COUNT": ("REG_BUTTON_COUNT",),
    "TICK CAPTURE": ("REG_TICK_CAPTURE",),
    "TICK 0..3": ("REG_TICK_0", "REG_TICK_1", "REG_TICK_2", "REG_TICK_3"),
}

VHDL_CONSTANT = re.compile(
    r'^\s*constant\s+(\w+)\s*:\s*byte_t\s*:=\s*x"([0-9A-Fa-f]{2})"\s*;', re.MULTILINE
)
C_DEFINE = re.compile(
    r"#define\s+((?:REG|CMD)_\w+)\s*\(\s*\(\s*uint8_t\s*\)\s*0x([0-9A-Fa-f]{1,2})u?\s*\)"
)
C_ENUM_MEMBER = re.compile(r"^\s*((?:REG|CMD)_\w+)\s*=\s*0x([0-9A-Fa-f]{1,2})u?\s*,", re.MULTILINE)
DESIGN_ID_DEFINE = re.compile(
    r"#define\s+BSP_FPGA_DESIGN_ID\s*\(\s*\(\s*uint8_t\s*\)\s*0x([0-9A-Fa-f]{1,2})u?\s*\)"
)
DOC_HEX = re.compile(r"^0x([0-9A-Fa-f]{1,2})$")
DOC_HEX_RANGE = re.compile(r"^0x([0-9A-Fa-f]{1,2})\.\.0x([0-9A-Fa-f]{1,2})$")
# The Ping row's Transaction cell reads "Return design ID `0xb8`". This regex
# runs against the NORMALIZED cell: parse_doc_tables strips a cell's outer
# backticks, which eats this phrase's closing backtick exactly when the phrase
# ends the cell -- so the accepted grammar is "design ID `0x" + exactly two hex
# digits + (the closing backtick, or the end of the normalized cell). Exactly
# two digits with a required boundary is the point: "0xb80" and "0xb8garbage"
# must fail rather than be read as 0xb8, and "0xb" must fail rather than pass
# as a one-digit value.
DOC_PING_DESIGN_ID = re.compile(r"design ID `0x([0-9A-Fa-f]{2})(?:`|$)")
IBIT_PING = re.compile(r"ping returns `0x([0-9A-Fa-f]{2})`")
SCRIPT_ID = re.compile(r"id=([0-9A-Fa-f]{2}) ")
SCRIPT_HELLO = re.compile(r"FPGA ([0-9A-Fa-f]{2})\$")


def parse_vhdl_constants(text: str) -> dict[str, int]:
    return {name: int(value, 16) for name, value in VHDL_CONSTANT.findall(text)}


def parse_c_constants(text: str) -> dict[str, int]:
    found: dict[str, int] = {}
    for pattern in (C_DEFINE, C_ENUM_MEMBER):
        for name, value in pattern.findall(text):
            found[name] = int(value, 16)
    return found


def parse_doc_tables(text: str) -> list[list[str]]:
    """Return the stripped cells of every markdown table row.

    The command table is label-first (| Write | 0x02 | ...) and the register
    table is address-first (| 0x01 | STATUS | ...); callers pick the columns.
    """
    rows: list[list[str]] = []
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip().strip("`").strip() for cell in line.strip("|").split("|")]
        if len(cells) >= 2:
            rows.append(cells)
    return rows


def check_vhdl(errors: list[str]) -> dict[str, int]:
    vhdl = parse_vhdl_constants(VHDL_PACKAGE.read_text(encoding="utf-8"))
    missing = VHDL_NAMES.difference(vhdl)
    unexpected = set(vhdl).difference(VHDL_NAMES)
    if missing:
        errors.append(f"{VHDL_PACKAGE.name}: constants not parsed: {sorted(missing)}")
    if unexpected:
        errors.append(
            f"{VHDL_PACKAGE.name}: byte_t constants unknown to this check "
            f"(update its tables): {sorted(unexpected)}"
        )

    if not missing:
        if vhdl["REG_TICK_CAPTURE"] != vhdl["REG_TICK_0"]:
            errors.append(
                f"{VHDL_PACKAGE.name}: REG_TICK_CAPTURE and REG_TICK_0 must alias "
                "(write and read views of one address)"
            )
        registers = {
            name: value
            for name, value in vhdl.items()
            if name.startswith("REG_") and name != "REG_TICK_CAPTURE"
        }
        by_value: dict[int, list[str]] = {}
        for name, value in registers.items():
            by_value.setdefault(value, []).append(name)
        for value, names in sorted(by_value.items()):
            if len(names) > 1:
                errors.append(
                    f"{VHDL_PACKAGE.name}: registers collide at 0x{value:02x}: {sorted(names)}"
                )
    return vhdl


def check_c_files(vhdl: dict[str, int], errors: list[str]) -> None:
    for relative, expected in sorted(C_FILE_NAMES.items()):
        path = ROOT / relative
        found = parse_c_constants(path.read_text(encoding="utf-8"))
        missing = expected.difference(found)
        unexpected = set(found).difference(expected)
        if missing:
            errors.append(f"{relative}: constants not parsed: {sorted(missing)}")
        if unexpected:
            errors.append(
                f"{relative}: protocol constants unknown to this check "
                f"(update its tables): {sorted(unexpected)}"
            )
        for name in sorted(expected.intersection(found)):
            vhdl_value = vhdl.get(C_NAME_TO_VHDL[name])
            if vhdl_value is not None and found[name] != vhdl_value:
                errors.append(
                    f"{relative}: {name} is 0x{found[name]:02x} but "
                    f"forgix_pkg.vhd says 0x{vhdl_value:02x}"
                )


def check_design_id_header(vhdl: dict[str, int], errors: list[str]) -> None:
    matches = DESIGN_ID_DEFINE.findall(BSP_FPGA_HEADER.read_text(encoding="utf-8"))
    if len(matches) != 1:
        errors.append(f"{BSP_FPGA_HEADER.name}: expected exactly one BSP_FPGA_DESIGN_ID define")
        return
    value = int(matches[0], 16)
    expected = vhdl.get("DESIGN_ID")
    if expected is not None and value != expected:
        errors.append(
            f"{BSP_FPGA_HEADER.name}: BSP_FPGA_DESIGN_ID is 0x{value:02x} but "
            f"forgix_pkg.vhd says 0x{expected:02x}"
        )


def check_register_map(vhdl: dict[str, int], errors: list[str]) -> None:
    check_register_map_text(REGISTER_MAP.read_text(encoding="utf-8"), vhdl, errors)


def check_register_map_text(text: str, vhdl: dict[str, int], errors: list[str]) -> None:
    rows = parse_doc_tables(text)

    for label, vhdl_name in sorted(DOC_COMMANDS.items()):
        cells = [row[1] for row in rows if row[0] == label]
        if len(cells) != 1 or not DOC_HEX.match(cells[0] if cells else ""):
            errors.append(f"{REGISTER_MAP.name}: command row '{label}' not found exactly once")
            continue
        value = int(DOC_HEX.match(cells[0]).group(1), 16)
        expected = vhdl.get(vhdl_name)
        if expected is not None and value != expected:
            errors.append(
                f"{REGISTER_MAP.name}: command '{label}' is 0x{value:02x} but "
                f"forgix_pkg.vhd says 0x{expected:02x}"
            )

    for label, vhdl_names in sorted(DOC_REGISTERS.items()):
        cells = [row[0] for row in rows if row[1] == label]
        if len(cells) != 1:
            errors.append(f"{REGISTER_MAP.name}: register row '{label}' not found exactly once")
            continue
        cell = cells[0]
        range_match = DOC_HEX_RANGE.match(cell)
        single_match = DOC_HEX.match(cell)
        if range_match:
            first, last = (int(part, 16) for part in range_match.groups())
            addresses = list(range(first, last + 1))
        elif single_match:
            addresses = [int(single_match.group(1), 16)]
        else:
            errors.append(f"{REGISTER_MAP.name}: register row '{label}' has no address: {cell!r}")
            continue
        expected_addresses = [vhdl[name] for name in vhdl_names if name in vhdl]
        if len(expected_addresses) == len(vhdl_names) and addresses != expected_addresses:
            errors.append(
                f"{REGISTER_MAP.name}: register row '{label}' covers "
                f"{[f'0x{a:02x}' for a in addresses]} but forgix_pkg.vhd says "
                f"{[f'0x{a:02x}' for a in expected_addresses]}"
            )

    # Structural, not an occurrence count: the two places the document states
    # the design ID are the Ping row's Transaction cell and the ID row's
    # Meaning cell, and each is parsed and compared on its own. A count of
    # matching mentions anywhere in the file would be satisfied by prose while
    # both authoritative rows sat stale.
    design_id = vhdl.get("DESIGN_ID")
    if design_id is not None:
        ping_cells = [row[2] for row in rows if row[0] == "Ping" and len(row) >= 3]
        if len(ping_cells) != 1:
            errors.append(
                f"{REGISTER_MAP.name}: expected exactly one Ping row with a Transaction cell"
            )
        else:
            stated = DOC_PING_DESIGN_ID.findall(ping_cells[0])
            if len(stated) != 1:
                errors.append(
                    f"{REGISTER_MAP.name}: the Ping row must state 'design ID `0x..`' "
                    f"exactly once; its Transaction cell is {ping_cells[0]!r}"
                )
            elif int(stated[0], 16) != design_id:
                errors.append(
                    f"{REGISTER_MAP.name}: the Ping row says the design ID is "
                    f"0x{int(stated[0], 16):02x} but forgix_pkg.vhd says 0x{design_id:02x}"
                )

        id_cells = [row[3] for row in rows if len(row) >= 4 and row[1] == "ID"]
        if len(id_cells) != 1:
            errors.append(
                f"{REGISTER_MAP.name}: expected exactly one ID register row with a Meaning cell"
            )
        else:
            id_match = DOC_HEX.match(id_cells[0])
            if not id_match:
                errors.append(
                    f"{REGISTER_MAP.name}: the ID row's Meaning cell must be the bare "
                    f"design-ID value; it is {id_cells[0]!r}"
                )
            elif int(id_match.group(1), 16) != design_id:
                errors.append(
                    f"{REGISTER_MAP.name}: the ID row says the design ID is "
                    f"0x{int(id_match.group(1), 16):02x} but forgix_pkg.vhd says "
                    f"0x{design_id:02x}"
                )


def check_design_id_stragglers(vhdl: dict[str, int], errors: list[str]) -> None:
    design_id = vhdl.get("DESIGN_ID")
    if design_id is None:
        return

    ibit_matches = IBIT_PING.findall(IBIT_DOC.read_text(encoding="utf-8"))
    if len(ibit_matches) != 1:
        errors.append(f"{IBIT_DOC.name}: expected exactly one 'ping returns `0x..`' mention")
    elif int(ibit_matches[0], 16) != design_id:
        errors.append(
            f"{IBIT_DOC.name}: ping is documented to return 0x{int(ibit_matches[0], 16):02x} "
            f"but forgix_pkg.vhd says 0x{design_id:02x}"
        )

    script_text = HARDWARE_SCRIPT.read_text(encoding="utf-8")
    id_matches = SCRIPT_ID.findall(script_text)
    hello_matches = SCRIPT_HELLO.findall(script_text)
    if len(id_matches) < 2 or len(hello_matches) < 1:
        errors.append(
            f"{HARDWARE_SCRIPT.name}: expected at least two 'id=..' and one 'FPGA ..' "
            "design-ID patterns"
        )
    for value in id_matches + hello_matches:
        if int(value, 16) != design_id:
            errors.append(
                f"{HARDWARE_SCRIPT.name}: expects design ID 0x{int(value, 16):02x} but "
                f"forgix_pkg.vhd says 0x{design_id:02x}"
            )


def run_all_checks() -> list[str]:
    errors: list[str] = []
    vhdl = check_vhdl(errors)
    check_c_files(vhdl, errors)
    check_design_id_header(vhdl, errors)
    check_register_map(vhdl, errors)
    check_design_id_stragglers(vhdl, errors)
    return errors


def main() -> int:
    errors = run_all_checks()
    for error in errors:
        print(f"error: {error}")
    if errors:
        return 1
    checked = len(VHDL_NAMES) + sum(len(names) for names in C_FILE_NAMES.values())
    print(f"Protocol constants passed: {checked} name checks across VHDL, C, docs, and scripts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
