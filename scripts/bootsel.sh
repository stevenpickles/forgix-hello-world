#!/usr/bin/env bash
# Place the Forgix board in BOOTSEL mode, and confirm it actually got there.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

check_only=0
timeout_seconds=10

usage() {
  cat <<'USAGE'
Usage: ./scripts/bootsel.sh [--check] [--timeout <seconds>]

Reboots the board into BOOTSEL mode with `picotool reboot -f -u`, then polls
until the bootloader device appears, so a success message means the board is
ready to flash rather than merely that the command ran.

  --check              Report the current state and exit. Never changes it.
  --timeout <seconds>  How long to wait for BOOTSEL to appear (default 10).

Exit status is 0 when the board is in BOOTSEL mode, 1 otherwise.
USAGE
}

while (( $# )); do
  case "$1" in
    --check) check_only=1; shift ;;
    --timeout) timeout_seconds="${2:-}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'Unknown argument: %s\n\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

if ! [[ "$timeout_seconds" =~ ^[0-9]+$ ]] || (( timeout_seconds == 0 )); then
  printf 'timeout must be a positive whole number of seconds\n' >&2
  exit 2
fi

if ! command -v picotool >/dev/null 2>&1; then
  printf 'picotool was not found.\n' >&2
  printf 'Build the USB-enabled host utility with ./scripts/build_picotool.sh, or set\n' >&2
  printf 'PICOTOOL_BIN_PATH to the directory containing it.\n' >&2
  exit 1
fi

# The picotool the Pico SDK fetches into build/ is built without libusb and has
# no device commands at all, so check for the one this script needs.
if ! picotool help 2>&1 | grep -qE '^[[:space:]]+reboot[[:space:]]'; then
  printf 'picotool at %s has no reboot command, so it was built without USB support.\n' \
    "$(command -v picotool)" >&2
  printf 'Build the USB-enabled utility with ./scripts/build_picotool.sh.\n' >&2
  exit 1
fi

# `picotool info` without --force lists only devices already in BOOTSEL mode and
# never changes device state. Do not add --force to probe: it resets a running
# board to execute the command, which would silently end a soak run.
#
# picotool exits 0 whether or not it finds anything, so the output is the
# signal -- and it must be positive evidence. Matching on the absence of the
# "No accessible RP-series devices" string turned every libusb error, driver
# fault, and future wording change into a false "already in BOOTSEL". A device
# in BOOTSEL always prints a Program Information section, even over empty
# flash, so that heading is what presence means.
bootsel_present() {
  local output
  if ! output="$(picotool info 2>&1)"; then
    return 1
  fi
  [[ "$output" == *"Program Information"* ]]
}

print_manual_recovery() {
  cat >&2 <<'RECOVERY'

Fall back to the jumper procedure:

  1. Disconnect USB so the board is unpowered.
  2. Jumper the I/O-ring PRG (PROGRAM) pin to GND.
  3. Reconnect USB with the jumper fitted.
  4. Remove the jumper once the RP2350-family boot device appears.

See docs/picotool-windows.md#first-forgix-flash for the verified load procedure.
RECOVERY
}

if bootsel_present; then
  printf 'Board is already in BOOTSEL mode.\n'
  picotool info 2>&1 | sed 's/^/  /'
  exit 0
fi

if (( check_only )); then
  printf 'No board in BOOTSEL mode.\n'
  printf 'Run ./scripts/bootsel.sh to reboot a running board into it.\n'
  exit 1
fi

printf 'No board in BOOTSEL mode; requesting a reboot into it.\n'
# Worth stating plainly: this is step 5 of the Stage 4 checklist for a reason.
printf 'Note: this resets the board. Mid-soak, capture the passive observations\n'
printf '      first -- a reset discards the retained watchdog evidence.\n\n'

if ! picotool reboot -f -u; then
  printf '\npicotool could not reach the board.\n' >&2
  printf 'If it is running forgix_led_only_diagnostic, that is expected: USB is\n' >&2
  printf 'compiled out of that image entirely, so picotool can never talk to it.\n' >&2
  print_manual_recovery
  exit 1
fi

# picotool returning success only means the request was sent. Windows still has
# to tear down the CDC device and enumerate the bootloader.
printf '\nWaiting up to %ss for the bootloader to enumerate' "$timeout_seconds"
for (( elapsed = 0; elapsed < timeout_seconds; elapsed++ )); do
  if bootsel_present; then
    printf '\n\nBoard is in BOOTSEL mode.\n'
    picotool info 2>&1 | sed 's/^/  /'
    printf '\nFlash with ./scripts/flash.sh [image-name]\n'
    exit 0
  fi
  printf '.'
  sleep 1
done

printf '\n\nThe reboot was accepted but no BOOTSEL device appeared within %ss.\n' \
  "$timeout_seconds" >&2
printf 'Try a longer --timeout, or re-seat the USB cable.\n' >&2
print_manual_recovery
exit 1
