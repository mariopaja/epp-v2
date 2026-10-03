FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Kernel config fragment for drivers of external modules (merged on top of
# imx_v8_defconfig by kernel-yocto)
SRC_URI += "file://epp-v2.cfg"
