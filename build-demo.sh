#!/bin/bash
# Build a demo from a preset (presets/<name>.conf): the Zephyr DomU
# application and the Xen dom0less image with the preset's default
# passthrough (switchable on the board with epp-passthrough).
#
# Usage: ./build-demo.sh --list                     # show the presets
#        ./build-demo.sh <preset>                   # Zephyr + Yocto image
#        ./build-demo.sh <preset> --flash           # ... then ./flash-sd.sh
#        ./build-demo.sh <preset> --update [host]   # Zephyr only, to a running
#                       board: copy it as zephyr-domu.bin, select the preset's
#                       passthrough with epp-passthrough, reboot (host: Dom0,
#                       default EPP_BOARD_HOST from config.sh)
#
# Zephyr is built on the host in ZEPHYR_WORKSPACE (config.sh). Settings from
# the preset override board.conf for this build only; board.conf is not
# changed. SKIP_SYNC=1 skips the repo sync of the Yocto build as usual.
set -e
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

PRESET_DIR="${EPP_DIR}/presets"

die() { echo "build-demo: $*" >&2; exit 1; }

list_presets() {
    echo "Presets (${PRESET_DIR}):"
    for f in "${PRESET_DIR}"/*.conf; do
        (source "$f"; printf '  %-16s %s\n' "$(basename "$f" .conf)" "${DESCRIPTION}")
    done
}

case "$1" in
    ""|-h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; echo; list_presets; exit 0 ;;
    -l|--list) list_presets; exit 0 ;;
esac

PRESET="$1"
shift
[ -f "${PRESET_DIR}/${PRESET}.conf" ] || { list_presets >&2; die "unknown preset '${PRESET}'"; }

MODE=image
HOST="${EPP_BOARD_HOST}"
case "$1" in
    "") ;;
    --flash) MODE=flash ;;
    --update) MODE=update; [ -n "$2" ] && HOST="$2" ;;
    *) die "unknown option '$1' (--flash, --update [host])" ;;
esac

DESCRIPTION="" ZEPHYR_APP="" ZEPHYR_EXTRA="" PASSTHROUGH=none
source "${PRESET_DIR}/${PRESET}.conf"
APP_DIR="${EPP_DIR}/zephyr/samples/${ZEPHYR_APP}"
[ -d "${APP_DIR}" ] || die "Zephyr application not found: ${APP_DIR}"
case "${PASSTHROUGH}" in
    none|lpi2c4|lpi2c6) ;;
    *) die "${PRESET}: PASSTHROUGH must be none, lpi2c4 or lpi2c6 (got '${PASSTHROUGH}')" ;;
esac

echo "Preset:      ${PRESET}: ${DESCRIPTION}"
echo "Zephyr app:  ${ZEPHYR_APP}${ZEPHYR_EXTRA:+ + ${ZEPHYR_EXTRA}}"
if [ "${PASSTHROUGH}" = none ]; then
    echo "Passthrough: gpio2 (default; switch on the board with epp-passthrough)"
else
    echo "Passthrough: gpio2 ${PASSTHROUGH} (default; switch on the board with epp-passthrough)"
fi
echo ""

# --- Zephyr DomU application ----------------------------------------------
[ -d "${ZEPHYR_WORKSPACE}/zephyr" ] || die "no Zephyr workspace at ${ZEPHYR_WORKSPACE} (ZEPHYR_WORKSPACE in config.sh)"
if [ -f "${ZEPHYR_WORKSPACE}/.venv/bin/activate" ]; then
    source "${ZEPHYR_WORKSPACE}/.venv/bin/activate"
fi
command -v west >/dev/null || die "west not found (activate the Zephyr Python environment)"

# dom0less console and image header, plus the preset's extras
OVERLAYS="${EPP_DIR}/zephyr/xen/xenvm-dom0less.overlay"
CONFS="${EPP_DIR}/zephyr/xen/xenvm-dom0less.conf"
for x in ${ZEPHYR_EXTRA}; do
    [ -f "${APP_DIR}/${x}" ] || die "${PRESET}: ${APP_DIR}/${x} not found"
    case "$x" in
        *.overlay) OVERLAYS="${OVERLAYS};${APP_DIR}/${x}" ;;
        *.conf)    CONFS="${CONFS};${APP_DIR}/${x}" ;;
        *) die "${PRESET}: ZEPHYR_EXTRA '${x}' is neither .overlay nor .conf" ;;
    esac
done

ZEPHYR_BUILD="${DOCKER_WORKDIR}/zephyr-build/${PRESET}"
echo "=== Zephyr: west build -> ${ZEPHYR_BUILD}"
(cd "${ZEPHYR_WORKSPACE}" &&
 west build -p -b "${ZEPHYR_DOMU_BOARD}" -d "${ZEPHYR_BUILD}" "${APP_DIR}" -- \
    -DEXTRA_ZEPHYR_MODULES="${EPP_DIR}/zephyr" \
    -DEXTRA_DTC_OVERLAY_FILE="${OVERLAYS}" \
    -DEXTRA_CONF_FILE="${CONFS}")
mkdir -p "${EPP_DIR}/xen"
cp "${ZEPHYR_BUILD}/zephyr/zephyr.bin" "${EPP_DIR}/xen/zephyr.bin"
echo "=== Zephyr: xen/zephyr.bin"
echo ""

# --- Only the Zephyr image, to a running board -----------------------------
if [ "${MODE}" = update ]; then
    echo "=== Update ${HOST}: zephyr-domu.bin, passthrough ${PASSTHROUGH}, reboot"
    scp -o ConnectTimeout=10 "${EPP_DIR}/xen/zephyr.bin" "root@${HOST}:/tmp/zephyr-domu.bin"
    # Boot partition: partition 1 of the disk Dom0 runs from
    ssh -o ConnectTimeout=10 "root@${HOST}" sh -s <<EOF
set -e
root=\$(sed -n 's/.*root=\([^ ]*\).*/\1/p' /proc/cmdline)
mnt=\$(mktemp -d)
mount "\${root%[0-9]*}1" "\$mnt"
cp /tmp/zephyr-domu.bin "\$mnt/"
sync
umount "\$mnt"
rmdir "\$mnt"
epp-passthrough ${PASSTHROUGH}
reboot
EOF
    echo "Board reboots. Zephyr output in Dom0: xl dmesg | grep DOM2"
    exit 0
fi

# --- Yocto image (Xen dom0less) ---------------------------------------------
echo "=== Yocto: ./docker-run.sh --xen"
EPP_XEN_ZEPHYR_IMAGE="xen/zephyr.bin" EPP_XEN_ZEPHYR_PASSTHROUGH="${PASSTHROUGH}" \
    "${EPP_DIR}/docker-run.sh" --xen

if [ "${MODE}" = flash ]; then
    "${EPP_DIR}/flash-sd.sh"
fi
