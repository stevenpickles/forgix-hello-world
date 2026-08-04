#!/usr/bin/env bash
# Build and push the private unified Forgix build image from a locally
# downloaded Efinity tarball. Every other toolchain is fetched, checksum-pinned,
# by the Dockerfile itself.
#
#   ./ci/forgix-build/build_image.sh ~/Downloads/efinity-2026.1.132-linux-x64.tar.bz2
#
# The image contains the Efinity tree, and the Efinix license (license.txt
# 2.3(a)) forbids distributing or giving others access to the software, so the
# pushed package MUST stay private. This script refuses to push anywhere but
# ghcr.io, so a mistyped FORGIX_IMAGE cannot put the tools on a public
# registry; it cannot check the package's visibility for you, and that setting
# is what actually keeps it private -- see docs/fpga-ci.md.
#
# Tags are immutable CalVer (YYYYMMDD, FORGIX_IMAGE_TAG=YYYYMMDD.2 for a
# same-day rebuild). There is no floating `latest`: consumers pin by digest
# through the FORGIX_BUILD_IMAGE repository variable, so the tag is for
# humans and the digest is the pin. Tool versions live in the Dockerfile ARGs
# and are queryable from a built image via /etc/forgix-build-release.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
tarball="${1:-}"
version="${EFINITY_VERSION:-2026.1}"
image="${FORGIX_IMAGE:-ghcr.io/stevenpickles/forgix-build}"
tag="${FORGIX_IMAGE_TAG:-$(date +%Y%m%d)}"
push="${FORGIX_IMAGE_PUSH:-1}"

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
  printf 'docker is required to build the Forgix build image.\n' >&2
  exit 2
fi
# Checked before the build rather than before the push, so a bad destination
# costs nothing. Docker Hub is the dangerous default: an unqualified image name
# resolves there, and there is no such thing as an accidental private push.
case "$image" in
  ghcr.io/*/*) ;;
  *)
    printf 'Refusing to build the Forgix image for a destination outside GHCR: %s\n' "$image" >&2
    printf 'The image contains Efinity, and the license forbids giving anyone else access.\n' >&2
    printf 'Set FORGIX_IMAGE to ghcr.io/<your-account>/<package>.\n' >&2
    exit 2
    ;;
esac

# Docker Desktop is a native Windows binary, so under Git Bash it has to be
# handed Windows paths -- a POSIX one fails as "GetFileAttributesEx \c:". The
# same conversion must NOT reach the container side of a --volume argument,
# which is why MSYS path rewriting is disabled rather than relied upon.
if command -v cygpath >/dev/null 2>&1; then
  export MSYS_NO_PATHCONV=1
  host_path() { cygpath -w "$1"; }
else
  host_path() { printf '%s' "$1"; }
fi

# The Dockerfile uses heredocs (# syntax=docker/dockerfile:1), which need
# BuildKit; ancient engines fail fast with a clear error instead of
# misparsing.
export DOCKER_BUILDKIT=1

# The build context is the directory holding the tarball, so the multi-gigabyte
# archive is never copied into a temporary context first.
context="$(cd "$(dirname "$tarball")" && pwd)"
archive="$(basename "$tarball")"

printf 'Building %s:%s\n  tarball: %s\n  context: %s\n\n' \
  "$image" "$tag" "$archive" "$context"

docker build \
  --file "$(host_path "$repo_root/ci/forgix-build/Dockerfile")" \
  --build-arg "EFINITY_VERSION=$version" \
  --build-arg "EFINITY_TARBALL=$archive" \
  --tag "$image:$tag" \
  "$(host_path "$context")"

printf '\nSmoke-testing the image...\n'
docker run --rm \
  --volume "$(host_path "$repo_root"):/work" \
  "$image:$tag" \
  forgix-verify

if [[ "$push" != "1" ]]; then
  printf '\nFORGIX_IMAGE_PUSH=%s, not pushing.\n' "$push"
  exit 0
fi

printf '\nPushing %s:%s\n' "$image" "$tag"
printf 'Log in first if needed:\n'
printf '  echo $GHCR_PAT | docker login ghcr.io -u <github-user> --password-stdin\n\n'
docker push "$image:$tag"

digest="$(docker inspect --format '{{index .RepoDigests 0}}' "$image:$tag")"

cat <<EOF

Pushed as:
  $image:$tag
  $digest

Remaining one-time steps in the GitHub UI:

  1. Package visibility must be Private, and "Manage Actions access" must stay
     EMPTY. Granting a public repository access to a private package lets fork
     pull requests pull it -- see docs/fpga-ci.md.
     https://github.com/users/stevenpickles/packages/container/forgix-build/settings
  2. The GHCR_TOKEN repository secret (a PAT with read:packages, only that
     scope) already covers this package: the PAT is account-scoped. Fork pull
     requests are never issued secrets, which is what keeps the tools out of
     their reach.
  3. Point the workflows at this exact build by setting the repository
     variable FORGIX_BUILD_IMAGE to the digest-pinned reference:
       $digest
  4. The old ghcr.io/stevenpickles/efinity package stays untouched until a
     verified release has shipped from this image; it is the rollback asset.
EOF
