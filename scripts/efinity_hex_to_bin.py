#!/usr/bin/env python3
"""Convert an Efinity passive-SPI hexadecimal image to compact bytes."""

from __future__ import annotations

import argparse
from pathlib import Path


BITSTREAM_HEADER_BYTES = 256
# The smallest real T8F49 image is ~170 KB; 4 KB catches truncation without
# guessing device sizes.
BITSTREAM_MIN_BYTES = 4096
BITSTREAM_HEADER_MARKER = "Mode: passive"


def validate_bitstream(image: bytes, source: Path) -> None:
    # The first 256 bytes of every Efinity passive-SPI image are a NUL-padded
    # ASCII header (Version/Generated/.../Mode: passive) -- see
    # docs/fpga-ci.md. A truncated hex still converts cleanly and passes a
    # mere non-empty check, so length and header are the cheap proof this is
    # a whole bitstream rather than the front of one.
    if len(image) < BITSTREAM_MIN_BYTES:
        raise ValueError(
            f"converted image from {source} is implausibly small: "
            f"{len(image)} bytes (< {BITSTREAM_MIN_BYTES})"
        )
    try:
        header = image[:BITSTREAM_HEADER_BYTES].rstrip(b"\x00").decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError(
            f"image from {source} does not start with an ASCII Efinity header"
        ) from error
    if BITSTREAM_HEADER_MARKER not in header:
        raise ValueError(
            f"image from {source} lacks {BITSTREAM_HEADER_MARKER!r} in its header; "
            "not a passive-SPI Efinity bitstream"
        )


def convert_file(
    source: Path, destination: Path, validate_bitstream_image: bool = False
) -> int:
    text = source.read_text(encoding="ascii")
    compact = "".join(text.split())
    if not compact:
        raise ValueError(f"Efinity image is empty: {source}")
    try:
        image = bytes.fromhex(compact)
    except ValueError as error:
        raise ValueError(f"invalid Efinity hex image {source}: {error}") from error
    if validate_bitstream_image:
        validate_bitstream(image, source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(image)
    return len(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument(
        "--expect-bitstream",
        action="store_true",
        help="require a plausible passive-SPI bitstream, not just valid hex",
    )
    args = parser.parse_args()
    count = convert_file(
        args.source, args.destination, validate_bitstream_image=args.expect_bitstream
    )
    print(f"Wrote {count} bytes to {args.destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

