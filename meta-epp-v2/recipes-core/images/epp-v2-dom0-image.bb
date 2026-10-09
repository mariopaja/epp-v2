SUMMARY = "epp-v2 minimal Xen Dom0 image, runs epp-v2-image as DomU"
DESCRIPTION = "SD card image for ./docker-run.sh --xen=domu and --xen=dom0less: \
a small control Dom0 (Xen tools and backends only) on partition 2 and the \
epp-v2-image rootfs, started as Xen DomU, on partition 3."
LICENSE = "MIT"

require recipes-core/images/core-image-minimal.bb
require epp-v2-image-common.inc

REQUIRED_DISTRO_FEATURES = "xen"

IMAGE_INSTALL:append = " \
    kernel-modules \
    kernel-image \
    xen-tools \
    epp-v2-xen-dom0 \
"

# boot | Dom0 rootfs | DomU rootfs (epp-v2-image)
WKS_FILE = "epp-v2-xen-domu.wks.in"
EPP_DOMU_ROOTFS = "epp-v2-image-${MACHINE}.rootfs.ext4"
do_image_wic[depends] += "epp-v2-image:do_image_complete"

# Zephyr DomU (EPP_XEN_ZEPHYR_IMAGE): on the boot partition for boot.scr
IMAGE_BOOT_FILES:append = "${@' zephyr-domu.bin' if d.getVar('EPP_XEN_ZEPHYR_IMAGE_PATH') else ''}"
do_image_wic[depends] += "${@'epp-v2-xen-zephyr:do_deploy' if d.getVar('EPP_XEN_ZEPHYR_IMAGE_PATH') else ''}"
# Partial device tree for hardware passed through to the Zephyr DomU
IMAGE_BOOT_FILES:append = "${@' devicetree/epp-zephyr-pt.dtb;zephyr-domu-pt.dtb' if (d.getVar('EPP_XEN_ZEPHYR_IMAGE_PATH') and d.getVar('EPP_XEN_ZEPHYR_PASSTHROUGH')) else ''}"

# Tell Dom0 and DomU apart on the network and the prompt. /boot/Image is the
# DomU kernel for xl (/etc/xen/epp-v2.cfg); the kernel package installs it
# as /boot/Image-<version>.
epp_dom0_rootfs () {
    echo "${EPP_HOSTNAME}-dom0" > ${IMAGE_ROOTFS}${sysconfdir}/hostname
    if [ ! -e ${IMAGE_ROOTFS}/boot/Image ]; then
        ln -s "$(basename ${IMAGE_ROOTFS}/boot/Image-*)" ${IMAGE_ROOTFS}/boot/Image
    fi
}
ROOTFS_POSTPROCESS_COMMAND += "epp_dom0_rootfs"
