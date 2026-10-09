#!/bin/bash
# Runs INSIDE the build container (started by docker-run.sh).
# Syncs the i.MX BSP, sets up the build dir, adds meta-epp-v2, writes
# conf/auto.conf from board.conf and builds the image.
#
# Set SKIP_SYNC=1 to skip "repo sync" on rebuilds.
# EPP_XEN=no|dom0|domu|dom0less selects the Xen mode (docker-run.sh --xen=<mode>).
set -e

for var in EPP_DIR VERSION BRANCH MANIFEST REMOTE MACHINE DISTRO IMAGES DOCKER_WORKDIR DL_DIR SSTATE_DIR; do
    if [ -z "${!var}" ]; then
        echo "Error: ${var} is not set (run this script through docker-run.sh)" >&2
        exit 1
    fi
done

BUILD_DIR_NAME="build_${MACHINE}"

echo "========================================="
echo "Building epp-v2 i.MX Yocto BSP"
echo "========================================="
echo "Version:  ${VERSION} (${BRANCH})"
echo "Machine:  ${MACHINE}"
echo "Distro:   ${DISTRO}"
echo "Images:   ${IMAGES}"
echo "Build:    ${DOCKER_WORKDIR}/${BUILD_DIR_NAME}"
echo "========================================="

cd "${DOCKER_WORKDIR}"

if [ "${SKIP_SYNC}" = "1" ] && [ -d .repo ]; then
    echo "Skipping repo sync (SKIP_SYNC=1)"
else
    repo init -u "${REMOTE}" -b "${BRANCH}" -m "${MANIFEST}"
    repo sync -j"$(nproc)"
fi

# Set up (or re-enter) the build directory; both scripts cd into it.
# setup-environment unsets MACHINE and DISTRO, so keep a copy.
EPP_MACHINE="${MACHINE}"
EPP_DISTRO="${DISTRO}"
if [ -f "${BUILD_DIR_NAME}/conf/local.conf" ]; then
    source setup-environment "${BUILD_DIR_NAME}"
else
    EULA=1 MACHINE="${MACHINE}" DISTRO="${DISTRO}" source imx-setup-release.sh -b "${BUILD_DIR_NAME}"
fi
export MACHINE="${EPP_MACHINE}" DISTRO="${EPP_DISTRO}"

# Add meta-epp-v2 (idempotent)
LAYER_PATH="${EPP_DIR}/meta-epp-v2"
if ! grep -q "${LAYER_PATH}" conf/bblayers.conf; then
    echo "BBLAYERS += \"${LAYER_PATH}\"" >> conf/bblayers.conf
    echo "Added ${LAYER_PATH} to bblayers.conf"
fi

# Generate conf/auto.conf from board.conf (regenerated on every build, so
# edit board.conf / config.sh rather than auto.conf).
# EPP_M7_IMAGE from the environment (flash-sd.sh --m7) wins over board.conf
EPP_M7_IMAGE_ENV="${EPP_M7_IMAGE}"
source "${EPP_DIR}/board.conf"
EPP_M7_IMAGE="${EPP_M7_IMAGE_ENV:-${EPP_M7_IMAGE}}"

EPP_XEN="${EPP_XEN:-no}"
case "${EPP_XEN}" in
    no)            XEN_CONF='DISTRO_FEATURES:remove = "xen"' ;;
    dom0)          XEN_CONF="" ;;
    domu|dom0less) XEN_CONF=""
                   # The SD card image is the minimal Dom0 image; it embeds
                   # epp-v2-image as the DomU rootfs partition
                   IMAGES="${IMAGES/epp-v2-image/epp-v2-dom0-image}" ;;
    *)  echo "Error: EPP_XEN must be no, dom0, domu or dom0less (got '${EPP_XEN}')" >&2; exit 1 ;;
esac
echo "Xen:      ${EPP_XEN}"

# Cortex-M7 firmware for the boot container (EPP_M7_IMAGE in board.conf)
EPP_M7_IMAGE_PATH=""
M7_CONF=""
if [ "${EPP_M7_IMAGE}" = "none" ]; then
    # Boot container without any M7 image (flash_a55): the M7 stays off
    M7_CONF='IMXBOOT_TARGETS:forcevariable = "flash_a55"'
elif [ -n "${EPP_M7_IMAGE}" ]; then
    case "${EPP_M7_IMAGE}" in
        /*) EPP_M7_IMAGE_PATH="${EPP_M7_IMAGE}" ;;
        *)  EPP_M7_IMAGE_PATH="${EPP_DIR}/${EPP_M7_IMAGE}" ;;
    esac
    if [ ! -f "${EPP_M7_IMAGE_PATH}" ]; then
        echo "Error: EPP_M7_IMAGE not found: ${EPP_M7_IMAGE_PATH}" >&2
        echo "       (it must be inside ${EPP_DIR}, the build container sees nothing else)" >&2
        exit 1
    fi
    # :forcevariable, the machine conf (parsed after auto.conf) sets it with =
    M7_CONF='M4_DEFAULT_IMAGE_MX95:forcevariable = "epp-v2-m7.bin"'
fi
echo "M7:       ${EPP_M7_IMAGE:-NXP demo}"  # none: no M7 image

# Zephyr as second dom0less DomU (EPP_XEN_ZEPHYR_IMAGE in board.conf)
EPP_XEN_ZEPHYR_IMAGE_PATH=""
if [ -n "${EPP_XEN_ZEPHYR_IMAGE}" ] && [ "${EPP_XEN}" = "dom0less" ]; then
    case "${EPP_XEN_ZEPHYR_IMAGE}" in
        /*) EPP_XEN_ZEPHYR_IMAGE_PATH="${EPP_XEN_ZEPHYR_IMAGE}" ;;
        *)  EPP_XEN_ZEPHYR_IMAGE_PATH="${EPP_DIR}/${EPP_XEN_ZEPHYR_IMAGE}" ;;
    esac
    if [ ! -f "${EPP_XEN_ZEPHYR_IMAGE_PATH}" ]; then
        echo "Error: EPP_XEN_ZEPHYR_IMAGE not found: ${EPP_XEN_ZEPHYR_IMAGE_PATH}" >&2
        echo "       (it must be inside ${EPP_DIR}, the build container sees nothing else)" >&2
        exit 1
    fi
    case "${EPP_XEN_ZEPHYR_CPU}" in
        ""|[1-5]) ;;
        *) echo "Error: EPP_XEN_ZEPHYR_CPU must be empty or 1-5 (got '${EPP_XEN_ZEPHYR_CPU}')" >&2; exit 1 ;;
    esac
    echo "Zephyr DomU: ${EPP_XEN_ZEPHYR_IMAGE} (${EPP_XEN_ZEPHYR_VCPUS} vCPU, ${EPP_XEN_ZEPHYR_MEM} MB, core ${EPP_XEN_ZEPHYR_CPU:-shared})"
    for pt in ${EPP_XEN_ZEPHYR_PASSTHROUGH}; do
        case "${pt}" in
            gpio2)  echo "Zephyr DomU passthrough: GPIO2 (RGB LED D19)" ;;
            lpi2c4) echo "Zephyr DomU passthrough: LPI2C4 (display I2C bus)" ;;
            *) echo "Error: EPP_XEN_ZEPHYR_PASSTHROUGH: unknown device '${pt}' (gpio2, lpi2c4)" >&2; exit 1 ;;
        esac
    done
    if [ -n "${EPP_XEN_ZEPHYR_PASSTHROUGH}" ]; then
        EPP_OVERLAYS="${EPP_OVERLAYS:+${EPP_OVERLAYS} }epp-zephyr-pt-pins.dtbo"
    fi
fi
echo "Images:   ${IMAGES}"

# Deterministic salt so the hash (and therefore the rootfs signature) only
# changes when the password or hostname changes.
SALT="$(printf '%s' "${EPP_HOSTNAME}${EPP_ROOT_PASSWORD}" | sha256sum | cut -c1-16)"
ROOT_HASH="$(openssl passwd -6 -salt "${SALT}" "${EPP_ROOT_PASSWORD}")"
# extrausers evaluates the parameters in a shell, so escape '$'
ROOT_HASH_ESCAPED="${ROOT_HASH//\$/\\\$}"

cat > conf/auto.conf <<EOF
# GENERATED by meta-epp-v2 scripts/yocto-build.sh from board.conf and config.sh.
# Do not edit, changes are overwritten on the next build.

MACHINE = "${MACHINE}"
DISTRO = "${DISTRO}"

DL_DIR = "${DL_DIR}"
SSTATE_DIR = "${SSTATE_DIR}"
# Keep the hash equivalence database next to the sstate cache, so the cache
# stays reusable when the build directory is deleted
BB_HASHSERVE_DB_DIR = "${SSTATE_DIR}"

BB_NUMBER_THREADS = "${BB_NUMBER_THREADS}"
PARALLEL_MAKE = "-j ${PARALLEL_MAKE_JOBS}"
BB_PRESSURE_MAX_MEMORY = "10000"
GNU_MIRROR = "https://ftp.gnu.org/gnu"

PACKAGE_CLASSES = "package_deb"

# epp-v2 distro policy (meta-epp-v2/conf/epp-v2.conf)
require conf/epp-v2.conf
${XEN_CONF}

# Board settings
EPP_HOSTNAME = "${EPP_HOSTNAME}"
EPP_ROOT_PASSWORD_HASH = "${ROOT_HASH_ESCAPED}"
EPP_SSH_ROOT_LOGIN = "${EPP_SSH_ROOT_LOGIN}"
EPP_OVERLAYS = "${EPP_OVERLAYS}"
EPP_FDTFILE = "${EPP_FDTFILE}"
EPP_M7_IMAGE_PATH = "${EPP_M7_IMAGE_PATH}"
${M7_CONF}
EPP_XEN = "${EPP_XEN}"
EPP_XEN_LINUX_VCPUS = "${EPP_XEN_LINUX_VCPUS}"
EPP_XEN_LINUX_MEM = "${EPP_XEN_LINUX_MEM}"
EPP_XEN_DOM0_VCPUS = "${EPP_XEN_DOM0_VCPUS}"
EPP_XEN_DOM0_MEM = "${EPP_XEN_DOM0_MEM}"
EPP_XEN_UPLINK = "${EPP_XEN_UPLINK}"
EPP_XEN_ZEPHYR_IMAGE_PATH = "${EPP_XEN_ZEPHYR_IMAGE_PATH}"
EPP_XEN_ZEPHYR_VCPUS = "${EPP_XEN_ZEPHYR_VCPUS}"
EPP_XEN_ZEPHYR_MEM = "${EPP_XEN_ZEPHYR_MEM}"
EPP_XEN_ZEPHYR_CPU = "${EPP_XEN_ZEPHYR_CPU}"
EPP_XEN_ZEPHYR_PASSTHROUGH = "${EPP_XEN_ZEPHYR_PASSTHROUGH}"
EPP_EXTRA_PACKAGES = "${EPP_EXTRA_PACKAGES}"
EOF

echo "Starting build..."
bitbake ${IMAGES}

# Shortcut to the output images: <repo>/images
DEPLOY_DIR="${DOCKER_WORKDIR}/${BUILD_DIR_NAME}/tmp/deploy/images/${MACHINE}"
ln -sfn "$(realpath --relative-to="${EPP_DIR}" "${DEPLOY_DIR}")" "${EPP_DIR}/images"

# images/epp-v2-sdcard.wic.zst (+ .bmap): the SD card image of this build,
# used by flash-sd.sh
# (not for IMAGES=imx-boot, used by flash-sd.sh --m7)
SDCARD_IMAGE="${IMAGES%% *}-${MACHINE}.rootfs.wic"
if [ -e "${DEPLOY_DIR}/${SDCARD_IMAGE}.zst" ]; then
    ln -sfn "${SDCARD_IMAGE}.zst" "${DEPLOY_DIR}/epp-v2-sdcard.wic.zst"
    ln -sfn "${SDCARD_IMAGE}.bmap" "${DEPLOY_DIR}/epp-v2-sdcard.wic.bmap"
fi

echo ""
echo "========================================="
echo "✅ Build completed successfully!"
echo "Images: ${EPP_DIR}/images -> ${DEPLOY_DIR}"
[ -e "${DEPLOY_DIR}/${SDCARD_IMAGE}.zst" ] && \
    echo "SD card: ${EPP_DIR}/images/epp-v2-sdcard.wic.zst -> ${SDCARD_IMAGE}.zst (Xen: ${EPP_XEN})"
echo "========================================="
