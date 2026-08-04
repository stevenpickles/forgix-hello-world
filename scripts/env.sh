# Tool locations for the Forgix build scripts. Source this file; do not run it.
#
#   source ./scripts/env.sh --print
#
# Every script that needs a tool path sources this, so the scripts work in a
# fresh Git Bash session with no setup. Sourcing it yourself is only needed when
# you want to invoke cmake, ninja, or picotool by hand.
#
# Variables already set in the environment always win, so CI and non-default
# installations are unaffected. Set any of these before sourcing to override:
#
#   EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH PICOTOOL_BIN_PATH PICO_TINYUSB_PATH
#
# Inside the forgix-build container (FORGIX_BUILD_CONTAINER=1) every one of
# them arrives pre-set from the image's ENV contract, so the Windows defaults
# below never apply there -- see ci/forgix-build/Dockerfile.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
  printf 'env.sh must be sourced, not executed:\n  source %s\n' "${BASH_SOURCE[0]}" >&2
  exit 1
fi

forgix_env_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

: "${EFINITY_HOME:=/c/Efinix/Efinity/2026.1}"
: "${PICO_SDK_PATH:=/c/RPi/pico-sdk-2.3.0}"
: "${GHDL_BIN_PATH:=/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin}"
export EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH

# picotool: prefer the USB-enabled install from ./scripts/build_picotool.sh. The
# copy the Pico SDK fetches into build/ is built without libusb -- it can convert
# a UF2 but has no `load` or `reboot`, so it must never be the first choice.
if [[ -z "${PICOTOOL_BIN_PATH:-}" ]]; then
  for forgix_env_candidate in \
      /c/RPi/picotool-2.3.0-install-usb/picotool \
      "$forgix_env_root/build/picotool-2.3.0/picotool"; do
    if [[ -x "$forgix_env_candidate/picotool.exe" || -x "$forgix_env_candidate/picotool" ]]; then
      PICOTOOL_BIN_PATH="$forgix_env_candidate"
      break
    fi
  done
  unset forgix_env_candidate
fi

if [[ -n "${PICOTOOL_BIN_PATH:-}" ]]; then
  export PICOTOOL_BIN_PATH
  # find_package(picotool) in the SDK reads this.
  export picotool_DIR="$PICOTOOL_BIN_PATH"
  case ":$PATH:" in
    *":$PICOTOOL_BIN_PATH:"*) ;;
    *) export PATH="$PICOTOOL_BIN_PATH:$PATH" ;;
  esac
fi

# TinyUSB ships as a Pico SDK submodule. A source archive without submodules
# needs either PICO_TINYUSB_PATH or a checkout in build/tinyusb.
if [[ -z "${PICO_TINYUSB_PATH:-}" ]]; then
  if command -v cygpath >/dev/null 2>&1; then
    forgix_env_sdk_posix="$(cygpath -u "$PICO_SDK_PATH" 2>/dev/null || printf '%s' "$PICO_SDK_PATH")"
  else
    forgix_env_sdk_posix="$PICO_SDK_PATH"
  fi
  for forgix_env_candidate in \
      "$forgix_env_sdk_posix/lib/tinyusb" \
      "$forgix_env_root/build/tinyusb"; do
    if [[ -f "$forgix_env_candidate/src/tusb.c" ]]; then
      export PICO_TINYUSB_PATH="$forgix_env_candidate"
      break
    fi
  done
  unset forgix_env_candidate forgix_env_sdk_posix
fi

forgix_env_report() {
  local name value
  printf 'Forgix tool environment\n'
  for name in EFINITY_HOME PICO_SDK_PATH GHDL_BIN_PATH PICOTOOL_BIN_PATH PICO_TINYUSB_PATH; do
    value="${!name:-}"
    if [[ -z "$value" ]]; then
      printf '  %-18s (not found)\n' "$name"
    elif [[ -e "$value" ]]; then
      printf '  %-18s %s\n' "$name" "$value"
    else
      printf '  %-18s %s  (missing)\n' "$name" "$value"
    fi
  done
}

if [[ "${1:-}" == "--print" ]]; then
  forgix_env_report
fi

unset forgix_env_root
