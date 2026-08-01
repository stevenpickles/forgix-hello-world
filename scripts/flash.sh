#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
uf2="$repo_root/build/firmware/forgix_hello_world.uf2"
[[ -f "$uf2" ]] || { printf 'UF2 not found: %s\n' "$uf2" >&2; exit 1; }
if ! picotool load -f "$uf2"; then
  printf 'picotool failed. Hold BOOTSEL while connecting USB, then copy %s to the RPI-RP2 drive.\n' "$uf2" >&2
  exit 1
fi
picotool reboot

