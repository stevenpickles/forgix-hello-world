#!/usr/bin/env python3
"""Convert an Efinity passive-SPI hexadecimal image to compact bytes."""

from __future__ import annotations

import argparse
from pathlib import Path


def convert_file(source: Path, destination: Path) -> int:
    text = source.read_text(encoding="ascii")
    compact = "".join(text.split())
    if not compact:
        raise ValueError(f"Efinity image is empty: {source}")
    try:
        image = bytes.fromhex(compact)
    except ValueError as error:
        raise ValueError(f"invalid Efinity hex image {source}: {error}") from error
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(image)
    return len(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    count = convert_file(args.source, args.destination)
    print(f"Wrote {count} bytes to {args.destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

