#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly ceedling_image="throwtheswitch/madsciencelab-plugins:1.0.1@sha256:fb29195d3148f6c6e324af380d5e7ea7030bf7a459d36b44e9cf04f1b6819cad"
readonly container_root="/home/dev/project"

if ! command -v docker >/dev/null 2>&1; then
  printf 'Docker is required to run the Ceedling test environment.\n' >&2
  exit 1
fi
if ! docker info >/dev/null 2>&1; then
  printf 'Docker is installed, but its engine is not available. Start Docker and retry.\n' >&2
  exit 1
fi

tasks=("$@")
if (( ${#tasks[@]} == 0 )); then
  tasks=(clobber test:all gcov:all)
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

printf 'Running Ceedling %s in %s\n' "${tasks[*]}" "$ceedling_image"
if command -v cygpath >/dev/null 2>&1; then
  MSYS_NO_PATHCONV=1 docker "${docker_args[@]}" "$ceedling_image" ceedling "${tasks[@]}"
else
  docker "${docker_args[@]}" "$ceedling_image" ceedling "${tasks[@]}"
fi
