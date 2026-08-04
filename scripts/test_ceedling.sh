#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

tasks=("$@")
if (( ${#tasks[@]} == 0 )); then
  tasks=(clobber test:all gcov:all)
fi

# Inside the unified build container the Ceedling toolchain is already present
# and docker is not: run it directly. FORGIX_BUILD_CONTAINER=1 is baked into
# the image environment (ci/forgix-build/Dockerfile).
if [[ "${FORGIX_BUILD_CONTAINER:-}" == "1" ]]; then
  cd "$repo_root/firmware"
  printf 'Running Ceedling %s inside the forgix-build container\n' "${tasks[*]}"
  exec ceedling "${tasks[@]}"
fi

# On a host, run the same image CI uses. Digest-pinned so the toolchain under
# the tests is byte-identical everywhere; the package is private, so this
# needs a one-time `docker login ghcr.io` with a read:packages PAT (see
# docs/fpga-ci.md).
readonly build_image="${FORGIX_BUILD_IMAGE:-ghcr.io/stevenpickles/forgix-build:20260804@sha256:f6adff0535766f4ff20e8dc2fa378905d8b2405ed4a315608a70b1b42d40de1e}"
readonly container_root="/work"

if ! command -v docker >/dev/null 2>&1; then
  printf 'Docker is required to run the Ceedling test environment.\n' >&2
  exit 1
fi
if ! docker info >/dev/null 2>&1; then
  printf 'Docker is installed, but its engine is not available. Start Docker and retry.\n' >&2
  exit 1
fi

docker_args=(
  run --rm
  --workdir "$container_root/firmware"
)

if command -v cygpath >/dev/null 2>&1; then
  mount_source="$(cygpath -w "$repo_root")"
else
  mount_source="$repo_root"
  docker_args+=(--user "$(id -u):$(id -g)" --env HOME=/tmp)
fi

docker_args+=(--volume "$mount_source:$container_root")

printf 'Running Ceedling %s in %s\n' "${tasks[*]}" "$build_image"
if command -v cygpath >/dev/null 2>&1; then
  MSYS_NO_PATHCONV=1 docker "${docker_args[@]}" "$build_image" ceedling "${tasks[@]}"
else
  docker "${docker_args[@]}" "$build_image" ceedling "${tasks[@]}"
fi
