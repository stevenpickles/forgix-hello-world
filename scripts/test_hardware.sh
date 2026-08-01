#!/usr/bin/env bash
set -euo pipefail

if ! command -v cygpath >/dev/null 2>&1 || ! command -v powershell.exe >/dev/null 2>&1; then
  printf 'The Forgix hardware smoke test requires Git Bash on Windows.\n' >&2
  exit 1
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_native="$(cygpath -w "$repo_root/scripts/test_hardware.ps1")"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$script_native" "$@"
