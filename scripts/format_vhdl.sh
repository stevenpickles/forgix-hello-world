#!/usr/bin/env bash
set -euo pipefail

# Applies the VHDL formatting rules with VSG (vhdl-style-guide). The rules themselves live in
# fpga/vsg.yaml; the parts a formatter cannot express are in docs/vhdl-style-rubric.md.
#
# Pass --check to verify without writing, which is what CI calls.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# pip's user-site install does not put vsg on PATH on Windows, so look there too.
vsg="${VSG:-}"
if [[ -z "$vsg" ]]; then
  for candidate in \
    "$(command -v vsg 2>/dev/null || true)" \
    "/c/Users/Steven/AppData/Roaming/Python/Python314/Scripts/vsg"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
      vsg="$candidate"
      break
    fi
  done
fi

if [[ -z "$vsg" ]]; then
  printf 'vsg not found. Install it with `pip install vsg==3.35.0` or set VSG to its path.\n' >&2
  exit 1
fi

mapfile -t sources < <(
  find "$repo_root/fpga/rtl" "$repo_root/fpga/tb" -type f -name '*.vhd' | sort
)

if (( ${#sources[@]} == 0 )); then
  printf 'No VHDL sources found.\n' >&2
  exit 1
fi

printf 'Using %s\n' "$("$vsg" --version)"

if [[ "${1:-}" == "--check" ]]; then
  # No --fix: vsg exits non-zero when there are violations, which is what makes this a gate.
  # -ap (--all_phases) matters: without it vsg stops at the first failing phase and
  # underreports violations by half.
  "$vsg" -c "$repo_root/fpga/vsg.yaml" -ap -f "${sources[@]}"
  printf 'VHDL formatting is clean: %d files\n' "${#sources[@]}"
  exit 0
fi

# -ap is rejected alongside --fix (vsg fixes phase by phase instead), so it is only used
# for --check above. vsg also exits non-zero here whenever a violation survives fixing --
# port_map_008 is `fixable: false` by design (see fpga/vsg.yaml), so a clean fix run can
# still report it. --check is the gate for that; this path only writes what it can.
"$vsg" -c "$repo_root/fpga/vsg.yaml" -f "${sources[@]}" --fix || true
printf 'Formatted %d files\n' "${#sources[@]}"
