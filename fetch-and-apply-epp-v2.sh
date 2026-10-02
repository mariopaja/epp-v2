#!/bin/bash
set -e
REPO_DIR="${HOME}/yocto-builds/epp-v2-yocto-config"
BUILD_DIR="${1:?Usage: fetch-and-apply-epp-v2.sh <path-to-build-dir>}"

if [ -d "$REPO_DIR/.git" ]; then
    git -C "$REPO_DIR" pull
else
    git clone https://github.com/mariopaja/epp-v2.git "$REPO_DIR"
fi

"$REPO_DIR/apply-config.sh" "$BUILD_DIR"
