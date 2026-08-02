#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

# Optional image name, without the .uf2 suffix. The lockup investigation also
# builds forgix_led_only_diagnostic.
image="${1:-forgix_hello_world}"
uf2="$repo_root/build/firmware/$image.uf2"
if [[ ! -f "$uf2" ]]; then
  printf 'UF2 not found: %s\n' "$uf2" >&2
  printf 'Available images:\n' >&2
  find "$repo_root/build/firmware" -maxdepth 1 -name '*.uf2' -exec basename {} .uf2 \; 2>/dev/null |
    sed 's/^/  /' >&2 || true
  printf 'Run ./scripts/build_firmware.sh first.\n' >&2
  exit 1
fi

if ! command -v picotool >/dev/null 2>&1; then
  printf 'picotool was not found.\n' >&2
  printf 'Build the USB-enabled host utility with ./scripts/build_picotool.sh, or set\n' >&2
  printf 'PICOTOOL_BIN_PATH to the directory containing it.\n' >&2
  exit 1
fi

# The picotool the Pico SDK fetches into build/ is compiled without libusb: it
# converts a UF2 but cannot talk to a device, so `load` is absent entirely. Catch
# that here rather than letting picotool report an unknown command.
if ! picotool help 2>&1 | grep -qE '^[[:space:]]+load[[:space:]]'; then
  printf 'picotool at %s has no load command, so it was built without USB support.\n' \
    "$(command -v picotool)" >&2
  printf 'Build the USB-enabled utility with ./scripts/build_picotool.sh, or point\n' >&2
  printf 'PICOTOOL_BIN_PATH at an installation that has it.\n' >&2
  exit 1
fi

printf 'Loading %s\n' "$uf2"
if ! picotool load -f "$uf2"; then
  printf '\npicotool could not load the image.\n' >&2
  printf 'Put the board in BOOTSEL with ./scripts/bootsel.sh and retry, or copy\n' >&2
  printf '%s to the RPI-RP2 drive by hand.\n' "$uf2" >&2
  exit 1
fi
picotool reboot
