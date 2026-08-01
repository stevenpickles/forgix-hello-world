#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export EFINITY_HOME="${EFINITY_HOME:-/c/Efinix/Efinity/2026.1}"
project="$repo_root/fpga/forgix_hello_world.xml"

if [[ ! -f "$project" ]]; then
  printf 'Efinity project metadata has not been added yet: %s\n' "$project" >&2
  exit 2
fi

efinity_win="$(cygpath -w "$EFINITY_HOME")"
project_win="$(cygpath -w "$project")"
cmd.exe /d /s /c "call \"$efinity_win\\bin\\setup.bat\" && efx_run.bat \"$project_win\" --flow full"

