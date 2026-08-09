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

# RTL analysis order is the dependency order -- GHDL has no dependency solver
# here, so this list stays explicit. The benches below are independent of each
# other and are discovered by name, so a new tb_*.vhd runs without anyone
# remembering to add it here; a bench whose entity does not match its filename
# fails at elaboration rather than being silently skipped.
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_pkg.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_button.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_rgb_pwm.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_spi.vhd"
"$ghdl" -a --std=08 "$repo_root/fpga/rtl/forgix_hello_world.vhd"
benches=("$repo_root"/fpga/tb/tb_*.vhd)
[[ -e "${benches[0]}" ]] || {
  printf 'no testbenches found under fpga/tb\n' >&2
  exit 1
}
for bench_file in "${benches[@]}"; do
  testbench="$(basename "$bench_file" .vhd)"
  "$ghdl" -a --std=08 "$bench_file"
  "$ghdl" -e --std=08 "$testbench"
  "$ghdl" -r --std=08 "$testbench" --assert-level=error
done
