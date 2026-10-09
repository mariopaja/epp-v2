SUMMARY = "epp-v2 minimal Xen Dom0 configuration"
DESCRIPTION = "xenbr0 bridge, Xen PV backends and the start of the epp-v2 \
DomU: by xendomains (--xen=domu) or attached to the DomU Xen created at boot \
(--xen=dom0less)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://10-xenbr0.netdev \
    file://10-xenbr0.network \
    file://10-xenbr0-uplink.network.in \
    file://10-xen-vif.network \
    file://xen-backends.conf \
    file://epp-v2-domu.cfg.in \
    file://epp-v2-wait-xenbr0 \
    file://xendomains-epp-v2.conf \
    file://epp-v2-dom0less.service.in \
    file://epp-v2-dom0less-setup.in \
"

S = "${UNPACKDIR}"

inherit systemd

# DomU rootfs: partition 3 of the SD card (epp-v2-xen-domu.wks.in)
EPP_DOMU_DISK ?= "/dev/mmcblk1p3"

SYSTEMD_SERVICE:${PN} = "epp-v2-dom0less.service"
SYSTEMD_AUTO_ENABLE:${PN} = "${@'enable' if d.getVar('EPP_XEN') == 'dom0less' else 'disable'}"

# Name of the Linux DomU in xl (xl list, xl console <name>)
EPP_LINUX_NAME = "${EPP_HOSTNAME}"

do_install[vardeps] += "EPP_XEN EPP_XEN_LINUX_MEM EPP_XEN_LINUX_VCPUS EPP_XEN_UPLINK EPP_DOMU_DISK \
                        EPP_LINUX_NAME"

do_install() {
    epp_subst() {
        sed -e 's|@@EPP_XEN_UPLINK@@|${EPP_XEN_UPLINK}|g' \
            -e 's|@@EPP_XEN_LINUX_MEM@@|${EPP_XEN_LINUX_MEM}|g' \
            -e 's|@@EPP_XEN_LINUX_VCPUS@@|${EPP_XEN_LINUX_VCPUS}|g' \
            -e 's|@@EPP_DOMU_DISK@@|${EPP_DOMU_DISK}|g' \
            -e 's|@@EPP_LINUX_NAME@@|${EPP_LINUX_NAME}|g' "$1"
    }

    install -d ${D}${sysconfdir}/systemd/network
    install -m 0644 ${S}/10-xenbr0.netdev ${S}/10-xenbr0.network ${S}/10-xen-vif.network \
        ${D}${sysconfdir}/systemd/network/
    epp_subst ${S}/10-xenbr0-uplink.network.in > ${D}${sysconfdir}/systemd/network/10-xenbr0-uplink.network

    install -d ${D}${sysconfdir}/modules-load.d
    install -m 0644 ${S}/xen-backends.conf ${D}${sysconfdir}/modules-load.d/

    install -d ${D}${libexecdir}
    install -m 0755 ${S}/epp-v2-wait-xenbr0 ${D}${libexecdir}/
    epp_subst ${S}/epp-v2-dom0less-setup.in > ${D}${libexecdir}/epp-v2-dom0less-setup
    chmod 0755 ${D}${libexecdir}/epp-v2-dom0less-setup

    # DomU config, also usable by hand: xl create /etc/xen/epp-v2.cfg
    install -d ${D}${sysconfdir}/xen/auto
    epp_subst ${S}/epp-v2-domu.cfg.in > ${D}${sysconfdir}/xen/epp-v2.cfg
    chmod 0644 ${D}${sysconfdir}/xen/epp-v2.cfg
    if [ "${EPP_XEN}" = "domu" ]; then
        ln -s ../epp-v2.cfg ${D}${sysconfdir}/xen/auto/epp-v2.cfg
    fi

    install -d ${D}${systemd_system_unitdir}/xendomains.service.d
    install -m 0644 ${S}/xendomains-epp-v2.conf ${D}${systemd_system_unitdir}/xendomains.service.d/epp-v2.conf
    epp_subst ${S}/epp-v2-dom0less.service.in > ${D}${systemd_system_unitdir}/epp-v2-dom0less.service
    chmod 0644 ${D}${systemd_system_unitdir}/epp-v2-dom0less.service
}

FILES:${PN} += "${systemd_system_unitdir}"
RDEPENDS:${PN} = "xen-tools-xl xen-tools-xendomains"
