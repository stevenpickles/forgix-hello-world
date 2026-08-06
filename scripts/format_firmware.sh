#!/usr/bin/env bash
set -euo pipefail

# Applies the firmware formatting rules with clang-format. The rules themselves live in
# firmware/.clang-format; the parts a formatter cannot express are in docs/firmware-style-rubric.md
# and scored by scripts/check_firmware_style.py.
#
# Pass --check to verify without writing, which is what CI should call.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/env.sh"

# The Windows LLVM installer does not put clang-format on PATH, so look in its
# fixed install locations too; a non-standard location pins CLANG_FORMAT in
# scripts/env.local.sh.
clang_format="${CLANG_FORMAT:-}"
if [[ -z "$clang_format" ]]; then
  for candidate in \
    "$(command -v clang-format 2>/dev/null || true)" \
    "/c/Program Files/LLVM/bin/clang-format.exe" \
    "/c/Program Files (x86)/LLVM/bin/clang-format.exe"; do
    if [[ -n "$candidate" && -x "$candidate" ]]; then
      clang_format="$candidate"
      break
    fi
  done
fi

if [[ -z "$clang_format" ]]; then
  printf 'clang-format not found. Install LLVM or set CLANG_FORMAT to its path.\n' >&2
  exit 1
fi

# firmware/build is a sibling of src and tests, so walking only those two trees keeps
# generated code out of reach without needing exclusion logic.
mapfile -t sources < <(
  find "$repo_root/firmware/src" "$repo_root/firmware/tests" \
    -type f \( -name '*.c' -o -name '*.h' \) | sort
)

if (( ${#sources[@]} == 0 )); then
  printf 'No firmware sources found.\n' >&2
  exit 1
fi

printf 'Using %s\n' "$("$clang_format" --version)"

if [[ "${1:-}" == "--check" ]]; then
  # -Werror turns "would reformat" into a non-zero exit, so this is a gate rather than a report.
  "$clang_format" --dry-run -Werror "${sources[@]}"
  printf 'Firmware formatting is clean: %d files\n' "${#sources[@]}"
  exit 0
fi

"$clang_format" -i "${sources[@]}"
printf 'Formatted %d files\n' "${#sources[@]}"
