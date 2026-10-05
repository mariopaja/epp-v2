SUMMARY = "epp-v2 Cortex-M7 firmware for the i.MX95 boot container"
DESCRIPTION = "Deploys EPP_M7_IMAGE (board.conf) as mcore-demos/epp-v2-m7.bin. \
imx-boot packs it as m7_image.bin into flash_all, so the System Manager \
starts it at power-on instead of the NXP M7 demo."
LICENSE = "CLOSED"

COMPATIBLE_MACHINE = "imx95-15x15-lpddr4x-frdm"

inherit deploy nopackages

# Absolute path, written to auto.conf by scripts/yocto-build.sh
EPP_M7_IMAGE_PATH ??= ""

# Rebuild (and repack imx-boot) when the firmware file changes
do_deploy[file-checksums] += "${@'${EPP_M7_IMAGE_PATH}:True' if d.getVar('EPP_M7_IMAGE_PATH') else ''}"

do_deploy() {
    if [ -z "${EPP_M7_IMAGE_PATH}" ]; then
        bbfatal "EPP_M7_IMAGE_PATH is not set (EPP_M7_IMAGE in board.conf)"
    fi
    install -D -m 0644 "${EPP_M7_IMAGE_PATH}" ${DEPLOYDIR}/mcore-demos/epp-v2-m7.bin
}
addtask deploy before do_build
