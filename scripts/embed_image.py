#!/usr/bin/env python3
"""Convert a compact FPGA binary to deterministic C source and header files."""

from pathlib import Path
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: embed_image.py INPUT.bin OUTPUT.c OUTPUT.h", file=sys.stderr)
        return 2
    source, c_path, h_path = map(Path, sys.argv[1:])
    data = source.read_bytes()
    if not data:
        raise SystemExit(f"FPGA image is empty: {source}")
    h_path.write_text(
        "#pragma once\n#include <stddef.h>\n#include <stdint.h>\n"
        "extern const uint8_t fpga_image[];\n"
        "extern const size_t fpga_image_size;\n",
        encoding="ascii",
    )
    rows = [", ".join(f"0x{value:02x}" for value in data[i : i + 12]) for i in range(0, len(data), 12)]
    body = ",\n    ".join(rows)
    c_path.write_text(
        '#include "fpga_image.h"\n\n'
        f"const uint8_t fpga_image[] = {{\n    {body}\n}};\n"
        "const size_t fpga_image_size = sizeof(fpga_image);\n",
        encoding="ascii",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

