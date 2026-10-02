#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:?Usage: apply-config.sh <path-to-build-dir>}"

# Add the custom layer (skipped if already present)
bitbake-layers add-layer "${SCRIPT_DIR}/meta-epp-v2" 2>/dev/null || true

# Append config only if not already applied
if ! grep -q "epp-v2 custom config" "${BUILD_DIR}/conf/local.conf"; then
    cat "${SCRIPT_DIR}/local.conf.append" >> "${BUILD_DIR}/conf/local.conf"
    echo "Applied epp-v2 config to ${BUILD_DIR}/conf/local.conf"
else
    echo "epp-v2 config already present, skipping"
fi
