SUMMARY = "epp-v2 image for the FRDM-IMX95 (LPDDR4x)"
LICENSE = "MIT"

require recipes-core/images/core-image-minimal.bb
require epp-v2-image-common.inc

IMAGE_INSTALL:append = " \
    kernel-modules \
    ${@'xen-tools' if d.getVar('EPP_XEN') == 'dom0' else ''} \
    ${EPP_EXTRA_PACKAGES} \
"

IMAGE_ROOTFS_EXTRA_SPACE = "4194304"

# With --xen=domu / --xen=dom0less this rootfs runs as Xen DomU: an ext4 image
# of it becomes partition 3 of the epp-v2-dom0-image SD card
IMAGE_FSTYPES:append = "${@' ext4' if d.getVar('EPP_XEN') in ('domu', 'dom0less') else ''}"
# meta-imx adds "xen xen-tools" to every image when Xen is enabled; a DomU
# does not need the Dom0 tools and services (xenstored, xendomains, ...)
IMAGE_INSTALL:remove = "${@'xen xen-tools' if d.getVar('EPP_XEN') in ('domu', 'dom0less') else ''}"
