#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work_dir="$repo_root/build/ghdl"
if [[ -n "${GHDL_BIN_PATH:-}" ]]; then
  ghdl="$GHDL_BIN_PATH/ghdl"
elif command -v ghdl >/dev/null 2>&1; then
  ghdl="$(command -v ghdl)"
else
  export GHDL_BIN_PATH='/c/Forgix/GHDL/ghdl-mcode-6.0.0-ucrt64/bin'
  ghdl="$GHDL_BIN_PATH/ghdl"
fi

[[ -x "$ghdl" || -x "$ghdl.exe" ]] || {
  printf 'GHDL executable not found: %s\n' "$ghdl" >&2
  exit 1
}

mkdir -p "$work_dir"
cd "$work_dir"

"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_pkg.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_button.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_rgb_pwm.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_spi.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_hello_world.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/tb/tb_button.vhd"
"$ghdl" -e --std=08 tb_button
"$ghdl" -r --std=08 tb_button --assert-level=error
