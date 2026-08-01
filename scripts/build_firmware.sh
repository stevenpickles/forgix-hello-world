#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PICO_SDK_PATH="${PICO_SDK_PATH:-/c/RPi/pico-sdk-2.3.0}"
build_dir="$repo_root/build/firmware"
fpga_image="$repo_root/fpga/outflow/forgix_hello_world.bin"
firmware_binary="$build_dir/forgix_hello_world.bin"
uf2="$build_dir/forgix_hello_world.uf2"

if [[ ! -s "$fpga_image" ]]; then
  printf 'FPGA image not found: %s\nRun ./scripts/build_fpga.sh first.\n' "$fpga_image" >&2
  exit 1
fi

repo_native="$(cygpath -w "$repo_root")"
build_native="$(cygpath -w "$build_dir")"
sdk_native="$(cygpath -w "$PICO_SDK_PATH")"
image_native="$(cygpath -w "$fpga_image")"
sdk_posix="$(cygpath -u "$PICO_SDK_PATH")"
tinyusb_posix="${PICO_TINYUSB_PATH:-$sdk_posix/lib/tinyusb}"
if [[ ! -f "$tinyusb_posix/src/tusb.c" && -f "$repo_root/build/tinyusb/src/tusb.c" ]]; then
  tinyusb_posix="$repo_root/build/tinyusb"
fi
if [[ ! -f "$tinyusb_posix/src/tusb.c" ]]; then
  printf 'TinyUSB is missing. Initialize lib/tinyusb in the Pico SDK or set PICO_TINYUSB_PATH.\n' >&2
  exit 1
fi
tinyusb_native="$(cygpath -w "$tinyusb_posix")"

cmake -S "$repo_native\\firmware" -B "$build_native" -G Ninja \
  -DPICO_SDK_PATH="$sdk_native" \
  -DPICO_TINYUSB_PATH="$tinyusb_native" \
  -DFPGA_IMAGE="$image_native"
cmake --build "$build_native"

[[ -s "$firmware_binary" ]] || {
  printf 'Firmware binary was not generated: %s\n' "$firmware_binary" >&2
  exit 1
}
[[ -s "$uf2" ]] || {
  printf 'UF2 was not generated. Install picotool 2.3.0 and rebuild.\n' >&2
  exit 1
}
firmware_size="$(wc -c < "$firmware_binary")"
if (( firmware_size > 2 * 1024 * 1024 )); then
  printf 'Firmware exceeds the RP2354 2 MB flash: %s bytes\n' "$firmware_size" >&2
  exit 1
fi
printf 'Firmware image: %s bytes\nUF2: %s\n' "$firmware_size" "$uf2"
