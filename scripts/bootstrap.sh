#!/usr/bin/env bash
set -u

default_efinity='/c/Efinix/Efinity/2026.1'
default_pico_sdk='/c/RPi/pico-sdk-2.3.0'
default_ghdl_bin='/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin'
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

export EFINITY_HOME="${EFINITY_HOME:-$default_efinity}"
export PICO_SDK_PATH="${PICO_SDK_PATH:-$default_pico_sdk}"
export GHDL_BIN_PATH="${GHDL_BIN_PATH:-$default_ghdl_bin}"

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
if command -v cygpath >/dev/null 2>&1; then
  pico_sdk_posix="$(cygpath -u "$PICO_SDK_PATH" 2>/dev/null || printf '%s' "$PICO_SDK_PATH")"
else
  pico_sdk_posix="$PICO_SDK_PATH"
fi
tinyusb_posix="${PICO_TINYUSB_PATH:-$pico_sdk_posix/lib/tinyusb}"
if [[ ! -f "$tinyusb_posix/src/tusb.c" && -f "$repo_root/build/tinyusb/src/tusb.c" ]]; then
  tinyusb_posix="$repo_root/build/tinyusb"
fi
if [[ -f "$tinyusb_posix/src/tusb.c" ]]; then
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
check_command picotool 'Build/install Raspberry Pi picotool and add it to PATH.'

if (( failures != 0 )); then
  printf '\nEnvironment is not ready: %d check(s) failed.\n' "$failures" >&2
  exit 1
fi

printf '\nEnvironment is ready.\n'
