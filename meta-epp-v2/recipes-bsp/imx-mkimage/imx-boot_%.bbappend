# With EPP_M7_IMAGE in board.conf, pack it as m7_image.bin into the boot
# container (M4_DEFAULT_IMAGE_MX95 is set in auto.conf by yocto-build.sh)
do_compile[depends] += "${@'epp-v2-m7-firmware:do_deploy' if d.getVar('EPP_M7_IMAGE_PATH') else ''}"
