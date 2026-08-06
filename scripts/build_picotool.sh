#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

# Machine-specific inputs come from the environment or scripts/env.local.sh;
# docs/picotool-windows.md records a known-good layout for every one of them.
# Only VsDevCmd keeps a default, because it is the VS2022 Community installer's
# fixed location rather than anyone's choice of disk layout.
picotool_source="${PICOTOOL_SOURCE:-}"
picotool_build="${PICOTOOL_BUILD:-${picotool_source:+$picotool_source-build-usb}}"
picotool_install="${PICOTOOL_INSTALL:-${picotool_source:+$picotool_source-install-usb}}"
pico_sdk_path="$PICO_SDK_PATH"
libusb_root="${LIBUSB_ROOT:-}"
libusb_include="${LIBUSB_INCLUDE_DIR:-${libusb_root:+$libusb_root/include}}"
libusb_library="${LIBUSB_LIBRARY:-${libusb_root:+$libusb_root/VS2019/MS64/static/libusb-1.0.lib}}"
cmake_executable="${CMAKE_EXE:-$(command -v cmake 2>/dev/null || true)}"
vsdevcmd="${VSDEVCMD_PATH:-/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/Tools/VsDevCmd.bat}"

missing=()
[[ -n "$picotool_source" ]] || missing+=(PICOTOOL_SOURCE)
[[ -n "$pico_sdk_path" ]] || missing+=(PICO_SDK_PATH)
[[ -n "$libusb_include" && -n "$libusb_library" ]] \
  || missing+=("LIBUSB_ROOT (or LIBUSB_INCLUDE_DIR and LIBUSB_LIBRARY)")
[[ -n "$cmake_executable" ]] || missing+=("CMAKE_EXE (no cmake found on PATH)")
if (( ${#missing[@]} )); then
  printf 'Not set: %s\n' "${missing[@]}" >&2
  printf 'Pin these in scripts/env.local.sh or export them before running.\n' >&2
  printf 'docs/picotool-windows.md records a known-good layout.\n' >&2
  exit 1
fi

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
