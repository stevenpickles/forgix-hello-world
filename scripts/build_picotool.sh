#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

picotool_source="${PICOTOOL_SOURCE:-/c/RPi/picotool-2.3.0}"
picotool_build="${PICOTOOL_BUILD:-/c/RPi/picotool-2.3.0-build-usb}"
picotool_install="${PICOTOOL_INSTALL:-/c/RPi/picotool-2.3.0-install-usb}"
pico_sdk_path="${PICO_SDK_PATH:-/c/RPi/pico-sdk-2.3.0}"
libusb_root="${LIBUSB_ROOT:-/c/Forgix/libusb-1.0.29}"
libusb_include="${LIBUSB_INCLUDE_DIR:-$libusb_root/include}"
libusb_library="${LIBUSB_LIBRARY:-$libusb_root/VS2019/MS64/static/libusb-1.0.lib}"
cmake_executable="${CMAKE_EXE:-/c/ST/STM32CubeCLT_1.20.0/CMake/bin/cmake.exe}"
vsdevcmd="${VSDEVCMD_PATH:-/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat}"

require_file() {
  local description="$1"
  local path="$2"
  if [[ ! -f "$path" ]]; then
    printf 'Missing %s: %s\n' "$description" "$path" >&2
    exit 1
  fi
}

require_dir() {
  local description="$1"
  local path="$2"
  if [[ ! -d "$path" ]]; then
    printf 'Missing %s: %s\n' "$description" "$path" >&2
    exit 1
  fi
}

if ! command -v cygpath >/dev/null 2>&1 || ! command -v cmd.exe >/dev/null 2>&1; then
  printf 'This script must run from Git Bash on Windows.\n' >&2
  exit 1
fi

require_dir 'picotool 2.3.0 source directory' "$picotool_source"
require_file 'picotool CMake project' "$picotool_source/CMakeLists.txt"
require_dir 'Pico SDK 2.3.0 directory' "$pico_sdk_path"
require_file 'Pico SDK version file' "$pico_sdk_path/pico_sdk_version.cmake"
require_dir 'libusb root directory' "$libusb_root"
require_file 'libusb header' "$libusb_include/libusb.h"
require_file 'Visual Studio x64 static libusb library' "$libusb_library"
require_file 'CMake executable' "$cmake_executable"
require_file 'Visual Studio 2022 developer environment script' "$vsdevcmd"

if ! grep -Eq 'set\(PICOTOOL_VERSION[[:space:]]+2\.3\.0\)' "$picotool_source/CMakeLists.txt"; then
  printf 'The source tree is not picotool 2.3.0: %s\n' "$picotool_source" >&2
  exit 1
fi

export PICOTOOL_SOURCE_NATIVE="$(cygpath -w "$picotool_source")"
export PICOTOOL_BUILD_NATIVE="$(cygpath -w "$picotool_build")"
export PICOTOOL_INSTALL_NATIVE="$(cygpath -w "$picotool_install")"
export PICO_SDK_PATH_NATIVE="$(cygpath -w "$pico_sdk_path")"
export LIBUSB_ROOT_NATIVE="$(cygpath -w "$libusb_root")"
export LIBUSB_INCLUDE_NATIVE="$(cygpath -w "$libusb_include")"
export LIBUSB_LIBRARY_NATIVE="$(cygpath -w "$libusb_library")"
export CMAKE_EXE_NATIVE="$(cygpath -w "$cmake_executable")"
export VSDEVCMD_NATIVE="$(cygpath -w "$vsdevcmd")"

helper_native="$(cygpath -w "$repo_root/scripts/build_picotool.cmd")"
cmd.exe //d //c "$helper_native"

picotool_bin="$picotool_install/picotool"
printf '\nUSB-enabled picotool installed successfully.\n'
printf 'For this Git Bash session, run:\n'
printf '  export PICOTOOL_BIN_PATH="%s"\n' "$picotool_bin"
printf '  export picotool_DIR="%s"\n' "$picotool_bin"
printf '  export PATH="$PICOTOOL_BIN_PATH:$PATH"\n'
