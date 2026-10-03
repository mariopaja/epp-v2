#!/bin/bash
# Build the Docker image that provides the Yocto build environment.
# Usage: ./docker-build.sh [--no-cache]
set -e
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

NO_CACHE=""
if [ "$1" = "--no-cache" ]; then
    NO_CACHE="--no-cache"
fi

echo "Building Docker image ${TAG} (Ubuntu ${UBUNTU})"

docker build \
    ${NO_CACHE} \
    --build-arg UBUNTU="${UBUNTU}" \
    --build-arg USER="$(whoami)" \
    --build-arg host_uid="$(id -u)" \
    --build-arg host_gid="$(id -g)" \
    -t "${TAG}" -f "${EPP_DIR}/Dockerfile" "${EPP_DIR}"

echo ""
echo "✅ Done: ${TAG}"
