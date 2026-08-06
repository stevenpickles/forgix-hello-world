#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"
# The per-platform build tree split lives in env.sh, shared with flash.sh so
# the builder and the flasher cannot disagree about where the images are.
build_dir="$FORGIX_FIRMWARE_BUILD_DIR"
fpga_image="$repo_root/fpga/outflow/forgix_hello_world.bin"

# Guarded here because CMake's own complaint about a missing SDK is a page of
# include errors rather than the name of the variable to set.
if [[ -z "$PICO_SDK_PATH" ]]; then
  printf 'PICO_SDK_PATH is not set. Pin it in scripts/env.local.sh or export it.\n' >&2
  exit 1
fi
firmware_binary="$build_dir/forgix_hello_world.bin"
uf2="$build_dir/forgix_hello_world.uf2"
led_only_uf2="$build_dir/forgix_led_only_diagnostic.uf2"

if [[ ! -s "$fpga_image" ]]; then
  printf 'FPGA image not found: %s\nRun ./scripts/build_fpga.sh first.\n' "$fpga_image" >&2
  exit 1
fi

# CMake on Windows is a native binary and must be handed Windows paths; in the
# forgix-build container (and any Linux host) the POSIX paths already are
# native -- the same split scripts/build_fpga.sh makes.
if command -v cygpath >/dev/null 2>&1; then
  native() { cygpath -w "$1"; }
  firmware_src="$(native "$repo_root")\\firmware"
else
  native() { printf '%s' "$1"; }
  firmware_src="$repo_root/firmware"
fi
build_native="$(native "$build_dir")"
sdk_native="$(native "$PICO_SDK_PATH")"
image_native="$(native "$fpga_image")"
tinyusb_posix="${PICO_TINYUSB_PATH:-}"
if [[ -z "$tinyusb_posix" || ! -f "$tinyusb_posix/src/tusb.c" ]]; then
  printf 'TinyUSB is missing. Initialize lib/tinyusb in the Pico SDK or set PICO_TINYUSB_PATH.\n' >&2
  exit 1
fi
tinyusb_native="$(native "$tinyusb_posix")"

cmake -S "$firmware_src" -B "$build_native" -G Ninja \
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
[[ -s "$led_only_uf2" ]] || {
  printf 'USB-free diagnostic UF2 was not generated: %s\n' "$led_only_uf2" >&2
  exit 1
}
firmware_size="$(wc -c < "$firmware_binary")"
if (( firmware_size > 2 * 1024 * 1024 )); then
  printf 'Firmware exceeds the RP2354 2 MB flash: %s bytes\n' "$firmware_size" >&2
  exit 1
fi
printf 'Firmware image: %s bytes\nUF2: %s\nUSB-free diagnostic UF2: %s\n' \
  "$firmware_size" "$uf2" "$led_only_uf2"
