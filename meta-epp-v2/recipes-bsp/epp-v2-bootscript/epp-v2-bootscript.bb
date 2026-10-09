SUMMARY = "epp-v2 U-Boot boot script with device tree overlay support"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

COMPATIBLE_MACHINE = "imx95-15x15-lpddr4x-frdm"

DEPENDS = "u-boot-mkimage-native"

SRC_URI = "file://boot.cmd.in"

S = "${UNPACKDIR}"

inherit deploy nopackages

# Xen resources: with --xen=dom0 the epp-v2 Linux is Dom0, with domu /
# dom0less a minimal Dom0 runs next to the epp-v2 Linux DomU
EPP_LINUX_IS_DOM0 = "${@'1' if d.getVar('EPP_XEN') == 'dom0' else ''}"
EPP_DOM0_MEM = "${@d.getVar('EPP_XEN_LINUX_MEM') if d.getVar('EPP_LINUX_IS_DOM0') else d.getVar('EPP_XEN_DOM0_MEM')}"
EPP_DOM0_VCPUS = "${@d.getVar('EPP_XEN_LINUX_VCPUS') if d.getVar('EPP_LINUX_IS_DOM0') else d.getVar('EPP_XEN_DOM0_VCPUS')}"
# Xen wants the dom0less DomU memory in KB
EPP_DOMU_MEM_KB = "${@hex(int(d.getVar('EPP_XEN_LINUX_MEM')) * 1024)}"

# Zephyr dom0less DomU (EPP_XEN_ZEPHYR_IMAGE): memory in KB for Xen
EPP_XEN_ZEPHYR = "${@'yes' if d.getVar('EPP_XEN_ZEPHYR_IMAGE_PATH') else 'no'}"
EPP_XEN_ZEPHYR_MEM_KB = "${@hex(int(d.getVar('EPP_XEN_ZEPHYR_MEM')) * 1024)}"
# Dedicated core for the Zephyr DomU: CPU node of core N is /cpus/cpu@N00
EPP_XEN_ZEPHYR_CPU_NODE = "${@('/cpus/cpu@%s00' % d.getVar('EPP_XEN_ZEPHYR_CPU')) if d.getVar('EPP_XEN_ZEPHYR_CPU') else ''}"
# Host device tree node passed through to the Zephyr DomU
EPP_XEN_ZEPHYR_PT_PATH = "${@{'gpio2': '/soc/gpio@43810000'}.get(d.getVar('EPP_XEN_ZEPHYR_PASSTHROUGH'), '')}"

# Rebuild the script when the board settings change
do_compile[vardeps] += "EPP_OVERLAYS EPP_FDTFILE EPP_XEN EPP_DOM0_MEM EPP_DOM0_VCPUS \
                        EPP_XEN_LINUX_MEM EPP_XEN_LINUX_VCPUS \
                        EPP_XEN_ZEPHYR EPP_XEN_ZEPHYR_MEM EPP_XEN_ZEPHYR_VCPUS \
                        EPP_XEN_ZEPHYR_CPU_NODE EPP_XEN_ZEPHYR_PT_PATH"

do_compile() {
    sed -e 's|@@EPP_OVERLAYS@@|${EPP_OVERLAYS}|g' \
        -e 's|@@EPP_FDTFILE@@|${EPP_FDTFILE}|g' \
        -e 's|@@EPP_XEN@@|${EPP_XEN}|g' \
        -e 's|@@EPP_DOM0_MEM@@|${EPP_DOM0_MEM}|g' \
        -e 's|@@EPP_DOM0_VCPUS@@|${EPP_DOM0_VCPUS}|g' \
        -e 's|@@EPP_DOMU_MEM@@|${EPP_XEN_LINUX_MEM}|g' \
        -e 's|@@EPP_DOMU_MEM_KB@@|${EPP_DOMU_MEM_KB}|g' \
        -e 's|@@EPP_DOMU_VCPUS@@|${EPP_XEN_LINUX_VCPUS}|g' \
        -e 's|@@EPP_XEN_ZEPHYR@@|${EPP_XEN_ZEPHYR}|g' \
        -e 's|@@EPP_XEN_ZEPHYR_MEM_KB@@|${EPP_XEN_ZEPHYR_MEM_KB}|g' \
        -e 's|@@EPP_XEN_ZEPHYR_MEM@@|${EPP_XEN_ZEPHYR_MEM}|g' \
        -e 's|@@EPP_XEN_ZEPHYR_VCPUS@@|${EPP_XEN_ZEPHYR_VCPUS}|g' \
        -e 's|@@EPP_XEN_ZEPHYR_CPU_NODE@@|${EPP_XEN_ZEPHYR_CPU_NODE}|g' \
        -e 's|@@EPP_XEN_ZEPHYR_PT_PATH@@|${EPP_XEN_ZEPHYR_PT_PATH}|g' \
        ${S}/boot.cmd.in > ${B}/boot.cmd
    mkimage -A arm64 -O linux -T script -C none -n "epp-v2 boot script" \
        -d ${B}/boot.cmd ${B}/boot.scr
}

do_deploy() {
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
addtask deploy after do_compile before do_build
