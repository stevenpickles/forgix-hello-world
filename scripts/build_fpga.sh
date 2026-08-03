#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"
project="$repo_root/fpga/forgix_hello_world.xml"
outflow="$repo_root/fpga/outflow"
hex_image="$outflow/forgix_hello_world.hex"
binary_image="$outflow/forgix_hello_world.bin"
pinout_report="$outflow/forgix_hello_world.pinout.rpt"
timing_report="$outflow/forgix_hello_world.timing.rpt"

if [[ ! -f "$project" ]]; then
  printf 'Efinity project metadata has not been added yet: %s\n' "$project" >&2
  exit 2
fi

efinity_win="$(cygpath -w "$EFINITY_HOME")"
repo_win="$(cygpath -w "$repo_root")"
wrapper_short="$(cygpath -u "$(cygpath -aw -s "$repo_root/scripts/run_efinity.cmd")")"

mkdir -p "$outflow"
rm -f \
  "$repo_root/fpga/forgix_hello_world.peri.xml" \
  "$outflow/forgix_hello_world.peri.db" \
  "$hex_image" \
  "$binary_image" \
  "$pinout_report" \
  "$timing_report"
"$wrapper_short" "$efinity_win" "$repo_win"

[[ -s "$hex_image" ]] || {
  printf 'Efinity did not produce a nonempty passive-SPI image: %s\n' "$hex_image" >&2
  exit 1
}
[[ -s "$pinout_report" ]] || {
  printf 'Efinity did not produce a pinout report.\n' >&2
  exit 1
}
[[ -s "$timing_report" ]] || {
  printf 'Efinity did not produce a static timing report.\n' >&2
  exit 1
}

python "$repo_root/scripts/check_efinity_reports.py" "$pinout_report" "$timing_report"

python "$repo_root/scripts/efinity_hex_to_bin.py" "$hex_image" "$binary_image"
[[ -s "$binary_image" ]] || {
  printf 'Converted FPGA image is empty: %s\n' "$binary_image" >&2
  exit 1
}

printf 'FPGA image: %s\n' "$binary_image"

