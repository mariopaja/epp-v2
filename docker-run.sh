#!/bin/bash
# Start the build container. With no arguments it runs the full Yocto build;
# pass "bash" (or any command) to get an interactive shell instead.
#
# Usage: ./docker-run.sh                  # full build (repo sync + bitbake), plain Linux
#        ./docker-run.sh --xen            # same as --xen=dom0less
#        ./docker-run.sh --xen=dom0less   # Xen starts the epp-v2 Linux DomU at boot
#        ./docker-run.sh --xen=domu       # Xen, minimal Dom0 starts the epp-v2 Linux DomU
#        ./docker-run.sh --xen=dom0       # Xen, epp-v2 Linux as Dom0
#        ./docker-run.sh bash             # interactive shell in the container
set -e
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

EPP_XEN=no
case "$1" in
    --xen) EPP_XEN=dom0less; shift ;;
    --xen=dom0|--xen=domu|--xen=dom0less) EPP_XEN="${1#--xen=}"; shift ;;
    --xen=*) echo "Unknown Xen mode '${1#--xen=}' (use dom0, domu or dom0less)" >&2; exit 1 ;;
esac

if [ $# -eq 0 ]; then
    set -- "${EPP_DIR}/scripts/yocto-build.sh"
fi

mkdir -p "${DOCKER_WORKDIR}" "${DL_DIR}" "${SSTATE_DIR}"

echo "Running ${TAG}"
echo "  Machine:  ${MACHINE}"
echo "  Distro:   ${DISTRO}"
echo "  Images:   ${IMAGES}"
echo "  Xen:      ${EPP_XEN}"
echo "  Workdir:  ${DOCKER_WORKDIR}"
echo ""

# This repo, the Yocto tree and the caches are mounted at the same paths as on
# the host, so paths in logs and bblayers.conf are identical inside and out.
DOCKER_ARGS=(
    --rm
    --volume "${EPP_DIR}:${EPP_DIR}"
    --volume "${DOCKER_WORKDIR}:${DOCKER_WORKDIR}"
    --volume "${DL_DIR}:${DL_DIR}"
    --volume "${SSTATE_DIR}:${SSTATE_DIR}"
    --workdir "${DOCKER_WORKDIR}"
)

# Interactive TTY only when there is one (allows running from CI / nohup)
if [ -t 0 ] && [ -t 1 ]; then
    DOCKER_ARGS+=(-it)
fi

[ -n "${DOCKER_CPUSET}" ] && DOCKER_ARGS+=(--cpuset-cpus="${DOCKER_CPUSET}")
[ -n "${DOCKER_MEMORY}" ] && DOCKER_ARGS+=(--memory="${DOCKER_MEMORY}" --memory-swap="${DOCKER_MEMORY_SWAP:-${DOCKER_MEMORY}}")

[ -d "${HOME}/.ssh" ] && DOCKER_ARGS+=(--volume "${HOME}/.ssh:${HOME}/.ssh:ro")
[ -f "${HOME}/.gitconfig" ] && DOCKER_ARGS+=(--volume "${HOME}/.gitconfig:${HOME}/.gitconfig:ro")

for var in EPP_DIR VERSION BRANCH MANIFEST REMOTE MACHINE DISTRO IMAGES \
           DOCKER_WORKDIR DL_DIR SSTATE_DIR BB_NUMBER_THREADS PARALLEL_MAKE_JOBS \
           SKIP_SYNC EPP_XEN; do
    DOCKER_ARGS+=(--env "${var}=${!var}")
done

START=$(date +%s)
set +e
docker run "${DOCKER_ARGS[@]}" "${TAG}" "$@"
RC=$?
END=$(date +%s)

D=$((END - START))
echo ""
echo "Started:  $(date -d @"${START}" '+%F %T')"
echo "Finished: $(date -d @"${END}" '+%F %T')"
printf 'Duration: %dh %02dm %02ds (exit code %d)\n' $((D / 3600)) $((D % 3600 / 60)) $((D % 60)) "${RC}"
exit "${RC}"
