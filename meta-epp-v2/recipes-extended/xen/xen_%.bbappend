FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Boot-time cpupools + null scheduler (dedicated CPU for the Zephyr DomU)
SRC_URI += "file://epp-v2-cpupools.cfg"
