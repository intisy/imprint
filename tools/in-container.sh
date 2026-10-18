#!/bin/sh
set -eu
# Git Bash prints /f/... for pwd, which Docker Desktop cannot mount; pwd -W prints F:/...
repository_root=$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))
docker build --quiet --file "$repository_root/ci/Dockerfile" --tag imprint-build "$repository_root/ci" >/dev/null
MSYS_NO_PATHCONV=1 docker run --rm --volume "$repository_root:/work" --workdir /work imprint-build "$@"
