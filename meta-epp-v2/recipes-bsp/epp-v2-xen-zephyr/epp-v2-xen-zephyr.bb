SUMMARY = "epp-v2 Zephyr image for the second Xen dom0less DomU"
DESCRIPTION = "Deploys EPP_XEN_ZEPHYR_IMAGE (board.conf) as zephyr-domu.bin. \
epp-v2-dom0-image puts it on the boot partition and boot.scr creates the \
dom0less domain /chosen/domU2 for it."
LICENSE = "CLOSED"

COMPATIBLE_MACHINE = "imx95-15x15-lpddr4x-frdm"

inherit deploy nopackages

# Absolute path, written to auto.conf by scripts/yocto-build.sh
EPP_XEN_ZEPHYR_IMAGE_PATH ??= ""

# Rebuild when the image file changes
do_deploy[file-checksums] += "${@'${EPP_XEN_ZEPHYR_IMAGE_PATH}:True' if d.getVar('EPP_XEN_ZEPHYR_IMAGE_PATH') else ''}"

do_deploy() {
    if [ -z "${EPP_XEN_ZEPHYR_IMAGE_PATH}" ]; then
        bbfatal "EPP_XEN_ZEPHYR_IMAGE_PATH is not set (EPP_XEN_ZEPHYR_IMAGE in board.conf)"
    fi
    install -D -m 0644 "${EPP_XEN_ZEPHYR_IMAGE_PATH}" ${DEPLOYDIR}/zephyr-domu.bin
}
addtask deploy before do_build
