#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"
work_dir="$repo_root/build/ghdl"
if [[ -x "$GHDL_BIN_PATH/ghdl" || -x "$GHDL_BIN_PATH/ghdl.exe" ]]; then
  ghdl="$GHDL_BIN_PATH/ghdl"
elif command -v ghdl >/dev/null 2>&1; then
  ghdl="$(command -v ghdl)"
else
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
for testbench in tb_button tb_pwm tb_spi_regs; do
  "$ghdl" -a --std=08 "$repo_root/fpga/tb/$testbench.vhd"
  "$ghdl" -e --std=08 "$testbench"
  "$ghdl" -r --std=08 "$testbench" --assert-level=error
done
