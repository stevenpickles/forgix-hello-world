#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Optional image name, without the .uf2 suffix. The lockup investigation also
# builds forgix_led_only_diagnostic.
image="${1:-forgix_hello_world}"
uf2="$repo_root/build/firmware/$image.uf2"
if [[ ! -f "$uf2" ]]; then
  printf 'UF2 not found: %s\n' "$uf2" >&2
  printf 'Available images:\n' >&2
  find "$repo_root/build/firmware" -maxdepth 1 -name '*.uf2' -exec basename {} .uf2 \; 2>/dev/null |
    sed 's/^/  /' >&2 || true
  exit 1
fi
if ! picotool load -f "$uf2"; then
  printf 'picotool failed. Hold BOOTSEL while connecting USB, then copy %s to the RPI-RP2 drive.\n' "$uf2" >&2
  exit 1
fi
picotool reboot
