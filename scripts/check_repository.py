#!/usr/bin/env python3
"""Validate repository-owned Efinity metadata without invoking licensed tools."""

from __future__ import annotations

from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parent.parent
FPGA_ROOT = ROOT / "fpga"
PROJECT = FPGA_ROOT / "forgix_hello_world.xml"
NAMESPACE = {"efx": "http://www.efinixinc.com/enf_proj"}


def referenced_paths(root: ET.Element) -> list[str]:
    references: list[str] = []
    for xpath in (
        ".//efx:design_file",
        ".//efx:sdc_file",
        ".//efx:isf_file",
    ):
        references.extend(
            element.attrib["name"]
            for element in root.findall(xpath, NAMESPACE)
            if element.attrib.get("name")
        )
    return references


def main() -> int:
    root = ET.parse(PROJECT).getroot()

    device = root.find("./efx:device_info/efx:device", NAMESPACE)
    timing_model = root.find("./efx:device_info/efx:timing_model", NAMESPACE)
    top = root.find("./efx:design_info/efx:top_module", NAMESPACE)
    if device is None or device.attrib.get("name") != "T8F49":
        raise ValueError("Efinity project must target the Forgix T8F49")
    if timing_model is None or timing_model.attrib.get("name") != "I2":
        raise ValueError("Efinity project must use the T8F49 I2 timing model")
    if top is None or top.attrib.get("name") != "forgix_hello_world":
        raise ValueError("unexpected Efinity top-level module")

    references = referenced_paths(root)
    required = {
        "rtl/forgix_hello_world.vhd",
        "constraints/forgix_hello_world.sdc",
        "constraints/forgix_hello_world_io.isf",
    }
    missing_metadata = required.difference(references)
    if missing_metadata:
        raise ValueError(f"Efinity project is missing required references: {missing_metadata}")

    fpga_root = FPGA_ROOT.resolve()
    for reference in references:
        path = (FPGA_ROOT / reference).resolve()
        if not path.is_relative_to(fpga_root):
            raise ValueError(f"Efinity reference escapes fpga/: {reference}")
        if not path.is_file():
            raise FileNotFoundError(f"Efinity reference does not exist: {reference}")

    print(f"Efinity metadata passed: {len(references)} referenced files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
