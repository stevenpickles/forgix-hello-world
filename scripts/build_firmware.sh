#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PICO_SDK_PATH="${PICO_SDK_PATH:-/c/RPi/pico-sdk-2.3.0}"
build_dir="$repo_root/build/firmware"
fpga_image="$repo_root/fpga/outflow/forgix_hello_world.bin"

if [[ ! -s "$fpga_image" ]]; then
  printf 'FPGA image not found: %s\nRun ./scripts/build_fpga.sh first.\n' "$fpga_image" >&2
  exit 1
fi

repo_native="$(cygpath -w "$repo_root")"
build_native="$(cygpath -w "$build_dir")"
sdk_native="$(cygpath -w "$PICO_SDK_PATH")"
image_native="$(cygpath -w "$fpga_image")"
cmake -S "$repo_native\\firmware" -B "$build_native" -G Ninja \
  -DPICO_SDK_PATH="$sdk_native" -DFPGA_IMAGE="$image_native"
cmake --build "$build_native"
printf 'UF2: %s\n' "$build_dir/forgix_hello_world.uf2"
