#!/usr/bin/env bash
# POSIX sibling of run_efinity.cmd: one headless Efinity compile.
#
#   ./scripts/run_efinity.sh EFINITY_HOME REPO_ROOT
#
# Sourcing setup.sh rewrites PATH, PYTHONHOME, and PYTHONPATH to point at the
# interpreter Efinity ships. That must not leak into the caller, which goes on to
# run the repository's own Python checks -- hence a separate process, exactly as
# the .cmd wrapper does on Windows.
set -euo pipefail

efinity_home="${1:-}"
repo_root="${2:-}"

if [[ -z "$efinity_home" || -z "$repo_root" ]]; then
  printf 'usage: %s EFINITY_HOME REPO_ROOT\n' "${BASH_SOURCE[0]}" >&2
  exit 2
fi
if [[ ! -f "$efinity_home/bin/setup.sh" ]]; then
  printf 'Efinity installation is missing bin/setup.sh: %s\n' "$efinity_home" >&2
  exit 2
fi

# setup.sh reads SYSTEM_LIBSTDCXX_VERSION before assigning it, so it aborts under
# `set -u`, and its pipelines are not written for pipefail: on Ubuntu 24.04 the
# source dies with SIGPIPE (141) under pipefail while passing under plain
# `set -eu`. Relax both only for the source.
set +u +o pipefail
# shellcheck disable=SC1091
source "$efinity_home/bin/setup.sh"
set -u -o pipefail

# setup.sh exports EFINITY_USER_DIR_INI=$HOME/.local/share/efinity/user_dir.ini,
# so it has to be pinned after the source rather than in the environment. $HOME
# is whatever the runner hands the container and is not guaranteed writable;
# this path is.
efinity_user_dir="${TMPDIR:-/tmp}/efinity"
mkdir -p "$efinity_user_dir"
export EFINITY_USER_DIR_INI="$efinity_user_dir/user_dir.ini"

cd "$repo_root/fpga"
exec efx_run.py --prj -f compile forgix_hello_world
