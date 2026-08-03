#!/usr/bin/env bash
# Build and push the private Efinity CI image from a locally downloaded tarball.
#
#   ./ci/efinity/build_image.sh ~/Downloads/efinity-2026.1.132.tar.bz2
#
# The Efinix license (license.txt 2.3(a)) forbids distributing or giving others
# access to the software, so the pushed package MUST stay private. This script
# refuses to run against a registry path that is not under your own account, but
# it cannot check package visibility for you -- see docs/fpga-ci.md.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tarball="${1:-}"
version="${EFINITY_VERSION:-2026.1}"
image="${EFINITY_IMAGE:-ghcr.io/stevenpickles/efinity}"
tag="${EFINITY_IMAGE_TAG:-$version}"
push="${EFINITY_IMAGE_PUSH:-1}"

if [[ -z "$tarball" ]]; then
  printf 'usage: %s PATH_TO_EFINITY_LINUX_TARBALL\n' "${BASH_SOURCE[0]}" >&2
  printf 'Download it from https://www.efinixinc.com/support/efinity.php (registration required).\n' >&2
  exit 2
fi
if [[ ! -f "$tarball" ]]; then
  printf 'Efinity tarball not found: %s\n' "$tarball" >&2
  exit 2
fi
if ! command -v docker >/dev/null 2>&1; then
  printf 'docker is required to build the Efinity CI image.\n' >&2
  exit 2
fi

# The build context is the directory holding the tarball, so the multi-gigabyte
# archive is never copied into a temporary context first.
context="$(cd "$(dirname "$tarball")" && pwd)"
archive="$(basename "$tarball")"

printf 'Building %s:%s\n  tarball: %s\n  context: %s\n\n' \
  "$image" "$tag" "$archive" "$context"

docker build \
  --file "$repo_root/ci/efinity/Dockerfile" \
  --build-arg "EFINITY_VERSION=$version" \
  --build-arg "EFINITY_TARBALL=$archive" \
  --tag "$image:$tag" \
  "$context"

printf '\nSmoke-testing the image against the repository project...\n'
docker run --rm \
  --volume "$repo_root:/work" \
  "$image:$tag" \
  bash -c 'set +u; . "$EFINITY_HOME/bin/setup.sh"; set -u; efx_run.py --help > /dev/null && echo "efx_run.py responds"'

if [[ "$push" != "1" ]]; then
  printf '\nEFINITY_IMAGE_PUSH=%s, not pushing.\n' "$push"
  exit 0
fi

printf '\nPushing %s:%s\n' "$image" "$tag"
printf 'Log in first if needed:\n'
printf '  echo $GHCR_PAT | docker login ghcr.io -u <github-user> --password-stdin\n\n'
docker push "$image:$tag"

cat <<EOF

Pushed. Two settings still have to be done once in the GitHub UI:

  1. Package visibility must be Private, and "Manage Actions access" must stay
     EMPTY. Granting a public repository access to a private package lets fork
     pull requests pull it -- see docs/fpga-ci.md.
     https://github.com/users/stevenpickles/packages/container/efinity/settings
  2. Store a PAT with read:packages (only that scope) as the repository secret
     GHCR_TOKEN. Fork pull requests are never issued secrets, which is what
     keeps the tools out of their reach.

Then the workflows resolve $image:$tag automatically.
EOF
