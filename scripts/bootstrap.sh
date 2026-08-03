#!/usr/bin/env bash
set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

failures=0

check_command() {
  local command_name="$1"
  local guidance="$2"
  if command -v "$command_name" >/dev/null 2>&1; then
    printf 'ok      %-20s %s\n' "$command_name" "$(command -v "$command_name")"
  else
    printf 'missing %-20s %s\n' "$command_name" "$guidance"
    failures=$((failures + 1))
  fi
}

normalize_dir() {
  local variable_name="$1"
  local value="$2"
  local posix_value="$value"
  if command -v cygpath >/dev/null 2>&1; then
    posix_value="$(cygpath -u "$value" 2>/dev/null || printf '%s' "$value")"
  fi
  if [[ -d "$posix_value" ]]; then
    printf 'ok      %-20s %s\n' "$variable_name" "$posix_value"
  else
    printf 'missing %-20s %s (set %s to its installation directory)\n' "$variable_name" "$value" "$variable_name"
    failures=$((failures + 1))
  fi
}

printf 'Forgix build environment\n'
normalize_dir EFINITY_HOME "$EFINITY_HOME"
normalize_dir PICO_SDK_PATH "$PICO_SDK_PATH"
normalize_dir GHDL_BIN_PATH "$GHDL_BIN_PATH"
tinyusb_posix="${PICO_TINYUSB_PATH:-}"
if [[ -n "$tinyusb_posix" && -f "$tinyusb_posix/src/tusb.c" ]]; then
  printf 'ok      %-20s %s\n' 'Pico SDK TinyUSB' "$tinyusb_posix"
else
  printf 'missing %-20s initialize lib/tinyusb in the SDK, or set PICO_TINYUSB_PATH\n' 'Pico SDK TinyUSB'
  failures=$((failures + 1))
fi
check_command python 'Install Python 3 and add it to PATH.'
check_command cmake 'Install CMake and add it to PATH.'
check_command ninja 'Install Ninja and add it to PATH.'
check_command arm-none-eabi-gcc 'Install the Arm GNU embedded toolchain and add it to PATH.'
if command -v cygpath >/dev/null 2>&1; then
  ghdl_bin_posix="$(cygpath -u "$GHDL_BIN_PATH" 2>/dev/null || printf '%s' "$GHDL_BIN_PATH")"
else
  ghdl_bin_posix="$GHDL_BIN_PATH"
fi
if [[ -x "$ghdl_bin_posix/ghdl.exe" || -x "$ghdl_bin_posix/ghdl" ]]; then
  printf 'ok      %-20s %s\n' 'ghdl' "$ghdl_bin_posix/ghdl"
else
  printf 'missing %-20s expected ghdl executable under %s\n' 'ghdl' "$GHDL_BIN_PATH"
  failures=$((failures + 1))
fi
# A picotool built without libusb has no load command, so it cannot flash. That
# is the copy the Pico SDK fetches into build/, and finding it first would leave
# flashing broken in a way that only shows up at the point of use.
if ! command -v picotool >/dev/null 2>&1; then
  printf 'missing %-20s run ./scripts/build_picotool.sh, or set PICOTOOL_BIN_PATH\n' 'picotool'
  failures=$((failures + 1))
elif picotool help 2>&1 | grep -qE '^[[:space:]]+load[[:space:]]'; then
  printf 'ok      %-20s %s\n' 'picotool' "$(command -v picotool)"
else
  printf 'missing %-20s %s has no load command (built without USB support)\n' \
    'picotool USB' "$(command -v picotool)"
  failures=$((failures + 1))
fi

if (( failures != 0 )); then
  printf '\nEnvironment is not ready: %d check(s) failed.\n' "$failures" >&2
  exit 1
fi

printf '\nEnvironment is ready.\n'
