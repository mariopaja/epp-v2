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
# Per passthrough combination (layer.conf, EPP_XEN_ZEPHYR_PT_COMBOS) a U-Boot
# command that sets the host nodes to pass through (epp_pt_paths) and their
# names (epp_pt_names); the default combination is EPP_XEN_ZEPHYR_PT_DEFAULT:
#   setenv epp_pt_lpi2c5_lpi2c6 'setenv epp_pt_paths "/soc/gpio@... /soc/... /soc/..."; setenv epp_pt_names gpio2,lpi2c5,lpi2c6'
python do_epp_pt_map() {
    fixed = d.getVar('EPP_XEN_ZEPHYR_PT_FIXED').split()
    combos = d.getVar('EPP_XEN_ZEPHYR_PT_COMBOS').split()
    default = d.getVar('EPP_XEN_ZEPHYR_PT_DEFAULT')
    if default not in combos:
        bb.fatal('EPP_XEN_ZEPHYR_PASSTHROUGH: %s cannot be selected together (EPP_XEN_ZEPHYR_PT_CONFLICTS: %s)'
                 % (default.replace('_', ' '), d.getVar('EPP_XEN_ZEPHYR_PT_CONFLICTS')))
    lines = []
    for opt in combos:
        devs = fixed + ([] if opt == 'none' else opt.split('_'))
        nodes = []
        for n in devs:
            node = d.getVarFlag('EPP_XEN_ZEPHYR_PT_NODE', n)
            if not node:
                bb.fatal('EPP_XEN_ZEPHYR_PT_NODE[%s] is not set (layer.conf)' % n)
            nodes.append(node)
        lines.append("setenv epp_pt_%s 'setenv epp_pt_paths \"%s\"; setenv epp_pt_names %s'"
                     % (opt, ' '.join(nodes), ','.join(devs)))
    with open(os.path.join(d.getVar('WORKDIR'), 'pt-map.cmd'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
}
# Varflags read by name in a loop are not tracked by bitbake: depend on them
EPP_PT_NODES_ALL = "${@' '.join('%s:%s' % (n, d.getVarFlag('EPP_XEN_ZEPHYR_PT_NODE', n)) for n in (d.getVar('EPP_XEN_ZEPHYR_PT_FIXED') + ' ' + d.getVar('EPP_XEN_ZEPHYR_PT_OPTIONS')).split())}"
do_epp_pt_map[vardeps] += "EPP_XEN_ZEPHYR_PT_FIXED EPP_XEN_ZEPHYR_PT_COMBOS EPP_PT_NODES_ALL \
                           EPP_XEN_ZEPHYR_PT_DEFAULT EPP_XEN_ZEPHYR_PT_CONFLICTS"
addtask epp_pt_map after do_configure before do_compile

# Rebuild the script when the board settings change
do_compile[vardeps] += "EPP_OVERLAYS EPP_FDTFILE EPP_XEN EPP_DOM0_MEM EPP_DOM0_VCPUS \
                        EPP_XEN_LINUX_MEM EPP_XEN_LINUX_VCPUS \
                        EPP_XEN_ZEPHYR EPP_XEN_ZEPHYR_MEM EPP_XEN_ZEPHYR_VCPUS \
                        EPP_XEN_ZEPHYR_CPU_NODE EPP_XEN_ZEPHYR_PT_DEFAULT"

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
        -e 's|@@EPP_XEN_ZEPHYR_PT_DEFAULT@@|${EPP_XEN_ZEPHYR_PT_DEFAULT}|g' \
        -e '/@@EPP_XEN_ZEPHYR_PT_MAP@@/{' -e 'r ${WORKDIR}/pt-map.cmd' -e 'd' -e '}' \
        ${S}/boot.cmd.in > ${B}/boot.cmd
    mkimage -A arm64 -O linux -T script -C none -n "epp-v2 boot script" \
        -d ${B}/boot.cmd ${B}/boot.scr
}

do_deploy() {
    install -m 0644 ${B}/boot.scr ${DEPLOYDIR}/boot.scr
}
addtask deploy after do_compile before do_build
