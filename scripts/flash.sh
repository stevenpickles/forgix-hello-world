#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

# Optional image name, without the .uf2 suffix. The lockup investigation also
# builds forgix_led_only_diagnostic. The build tree comes from env.sh, the same
# per-platform split build_firmware.sh writes into -- a hardcoded Windows path
# here made this script look in an empty directory on every other host.
image="${1:-forgix_hello_world}"
uf2="$FORGIX_FIRMWARE_BUILD_DIR/$image.uf2"
if [[ ! -f "$uf2" ]]; then
  printf 'UF2 not found: %s\n' "$uf2" >&2
  printf 'Available images:\n' >&2
  find "$FORGIX_FIRMWARE_BUILD_DIR" -maxdepth 1 -name '*.uf2' -exec basename {} .uf2 \; 2>/dev/null |
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

# Name the image being loaded. The two targets look alike on a bench supply --
# both rest on a blue 2 Hz heartbeat -- but only the USB-free one blinks its boot
# report, so flashing the wrong one silently invalidates a soak run.
image_name="$(picotool info "$uf2" 2>/dev/null | sed -n 's/^ *name: *//p' | head -1)"
if picotool info "$uf2" 2>/dev/null | grep -q 'USB stdin / stdout'; then
  image_kind='USB shell image: boot report goes to serial, no blink code'
else
  image_kind='USB-free image: boot report is an LED blink code, no serial'
fi
printf 'Loading %s\n  %s\n  %s\n' "$uf2" "${image_name:-<unnamed>}" "$image_kind"
if ! picotool load -v -f "$uf2"; then
  printf '\npicotool could not load the image.\n' >&2
  # picotool reaches a board either already in BOOTSEL or running an image that
  # exposes the USB reset interface. A USB-free image has neither, so once one is
  # flashed the PRG jumper is the only way back in -- and a failure here leaves
  # the previous image running, which looks identical on the LED.
  printf 'If the board is running a USB-free image, picotool cannot reach it at all.\n' >&2
  printf 'Power off, jumper the I/O-ring PRG pin to GND, reconnect USB, remove the\n' >&2
  printf 'jumper once the boot device appears, then run this again.\n' >&2
  exit 1
fi

# Confirm what is actually in flash now. A silently failed load leaves the old
# image running, and the two images are indistinguishable from the LED alone.
# Windows can take a few seconds to re-enumerate the device after the load, so
# poll rather than trusting the first empty answer -- and an answer that never
# comes is a verification that never ran, which must fail loudly rather than
# echo the requested name back as if it had been read out of the part.
flashed_name=""
for (( attempt = 0; attempt < 10; attempt++ )); do
  flashed_name="$(picotool info 2>/dev/null | sed -n 's/^ *name: *//p' | head -1)"
  [[ -n "$flashed_name" ]] && break
  sleep 1
done
if [[ -z "$flashed_name" ]]; then
  printf '\npicotool could not read back an image name, so verification did not run.\n' >&2
  printf 'The load may still have succeeded, but nothing here confirms it. Re-seat\n' >&2
  printf 'USB if needed and check with `picotool info` before trusting this flash.\n' >&2
  exit 1
fi
if [[ -n "$image_name" && "$flashed_name" != "$image_name" ]]; then
  printf '\nFlash reports %s but %s was requested.\n' "$flashed_name" "$image_name" >&2
  exit 1
fi
printf 'Verified in flash: %s\n' "$flashed_name"
picotool reboot
