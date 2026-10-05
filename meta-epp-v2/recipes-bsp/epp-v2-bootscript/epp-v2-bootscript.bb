SUMMARY = "epp-v2 U-Boot boot script with device tree overlay support"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "imx95-15x15-lpddr4x-frdm"

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://boot.cmd.in"

S = "${UNPACKDIR}"

inherit deploy nopackages

# Rebuild the script when the board settings change
do_compile[vardeps] += "EPP_OVERLAYS EPP_FDTFILE EPP_XEN"

do_compile() {
    sed -e 's|@@EPP_OVERLAYS@@|${EPP_OVERLAYS}|g' \
        -e 's|@@EPP_FDTFILE@@|${EPP_FDTFILE}|g' \
        -e 's|@@EPP_XEN@@|${EPP_XEN}|g' \
        ${S}/boot.cmd.in > ${B}/boot.cmd
    mkimage -A arm64 -O linux -T script -C none -n "epp-v2 boot script" \
        -d ${B}/boot.cmd ${B}/boot.scr
}

do_deploy() {
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
addtask deploy after do_compile before do_build
