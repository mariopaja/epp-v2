SUMMARY = "epp-v2 device tree overlays for external modules"
DESCRIPTION = "Builds every *.dtso in files/ against the kernel headers and \
deploys the .dtbo files. epp-v2-image puts them on the boot partition under \
overlays/, and the U-Boot boot script applies the ones listed in EPP_OVERLAYS."

inherit devicetree

COMPATIBLE_MACHINE = "imx95-15x15-lpddr4x-frdm"

SRC_URI = " \
    file://epp-example.dtso \
    file://epp-zephyr-gpio2-pins.dtso \
    file://epp-zephyr-gpio2-pt.dts \
"

# Start from an empty build directory, so overlays removed from SRC_URI are
# not deployed (and put on the boot partition) from an older build
do_compile[cleandirs] += "${B}"
