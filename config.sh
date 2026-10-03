#!/bin/bash
# Host-side build configuration (Docker + i.MX BSP release).
# Board-specific settings (hostname, root password, overlays) live in board.conf.

EPP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export EPP_DIR

# i.MX BSP release (Yocto wrynose)
export VERSION="${VERSION:-6.18.20-2.0.0}"
export BRANCH="${BRANCH:-imx-linux-wrynose}"
export MANIFEST="${MANIFEST:-imx-${VERSION}.xml}"
export REMOTE="${REMOTE:-https://github.com/nxp-imx/imx-manifest}"
export UBUNTU="${UBUNTU:-24.04}"

# Target
export MACHINE="${MACHINE:-imx95-15x15-lpddr4x-frdm}"
export DISTRO="${DISTRO:-fsl-imx-wayland}"
export IMAGES="${IMAGES:-epp-v2-image}"

# Where the Yocto tree (sources, build dir, caches) lives. Inside this repo but
# gitignored; it grows to hundreds of GB.
export DOCKER_WORKDIR="${DOCKER_WORKDIR:-${EPP_DIR}/yocto-builds}"
# Download and sstate caches (point them elsewhere to share across builds)
export DL_DIR="${DL_DIR:-${DOCKER_WORKDIR}/downloads}"
export SSTATE_DIR="${SSTATE_DIR:-${DOCKER_WORKDIR}/sstate-cache}"

# Docker image tag
export TAG="${TAG:-epp-v2-builder:imx-${VERSION}}"

# Container resource limits (leave empty for no limit).
# DOCKER_CPUSET restricts the container to some cores, e.g. "0-8,10-23".
export DOCKER_CPUSET="${DOCKER_CPUSET-}"
export DOCKER_MEMORY="${DOCKER_MEMORY-24g}"
# RAM + swap the container may use in total: 24 GB RAM + 8 GB swap, so short
# memory spikes slow the build down instead of killing it
export DOCKER_MEMORY_SWAP="${DOCKER_MEMORY_SWAP-32g}"

# BitBake parallelism inside the container. 8 tasks x 12 jobs keeps all 24
# cores busy within 24 GB; 24 x 24 ran out of memory (C++ compiles ~2 GB each).
export BB_NUMBER_THREADS="${BB_NUMBER_THREADS:-8}"
export PARALLEL_MAKE_JOBS="${PARALLEL_MAKE_JOBS:-12}"
