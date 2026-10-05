#!/bin/bash
# Flash the epp-v2 image to an SD card.
# Lists removable / USB / SD-reader disks, asks which one to use and writes the
# image with bmaptool (falls back to dd).
#
# Usage: ./flash-sd.sh [IMAGE.wic.zst]   # flash (default: SD card image of the last build)
#        ./flash-sd.sh --m7 zephyr.bin   # only replace the Cortex-M7 firmware: rebuild
#                                        # the boot container with it, write only that
#        ./flash-sd.sh --m7 none         # same, boot container without M7 image (M7 off)
#        ./flash-sd.sh --list            # only list candidate disks
set -e
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

LIST_ONLY=0
IMAGE=""
M7_IMAGE=""
while [ $# -gt 0 ]; do
    case "$1" in
        -l|--list) LIST_ONLY=1; shift ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        --m7) M7_IMAGE="$2"; shift 2 || { echo "--m7 needs a file" >&2; exit 1; } ;;
        --m7=*) M7_IMAGE="${1#--m7=}"; shift ;;
        *) IMAGE="$1"; shift ;;
    esac
done

if [ "${M7_IMAGE}" = "none" ] && [ "${LIST_ONLY}" = "0" ]; then
    echo "Building the boot container without M7 image ..."
    EPP_M7_IMAGE=none IMAGES=imx-boot SKIP_SYNC=1 "${EPP_DIR}/docker-run.sh"
    IMAGE="$(realpath "${EPP_DIR}/images/imx-boot")"
    echo ""
elif [ -n "${M7_IMAGE}" ] && [ "${LIST_ONLY}" = "0" ]; then
    # Rebuild only the boot container (imx-boot) with this M7 firmware. The
    # build container only sees this repo, so copy the file to m7/ if needed.
    if [ ! -f "${M7_IMAGE}" ]; then
        echo "M7 image not found: ${M7_IMAGE}"
        exit 1
    fi
    M7_IMAGE="$(realpath "${M7_IMAGE}")"
    case "${M7_IMAGE}" in
        "${EPP_DIR}"/*) ;;
        *)  mkdir -p "${EPP_DIR}/m7"
            cp "${M7_IMAGE}" "${EPP_DIR}/m7/"
            echo "Copied ${M7_IMAGE} to m7/"
            M7_IMAGE="${EPP_DIR}/m7/$(basename "${M7_IMAGE}")" ;;
    esac
    echo "Building the boot container with M7 firmware ${M7_IMAGE#${EPP_DIR}/} ..."
    EPP_M7_IMAGE="${M7_IMAGE#${EPP_DIR}/}" IMAGES=imx-boot SKIP_SYNC=1 "${EPP_DIR}/docker-run.sh"
    IMAGE="$(realpath "${EPP_DIR}/images/imx-boot")"
    echo ""
fi
[ -z "${IMAGE}" ] && IMAGE="${EPP_DIR}/images/epp-v2-sdcard.wic.zst"

# Disk that holds the running system: never offered
ROOT_DISK="$(lsblk -no PKNAME "$(findmnt -no SOURCE /)" 2>/dev/null | head -1)"

# Candidates: whole disks that are removable, hotplug, USB or MMC (SD reader)
CANDIDATES=()
while read -r name rm hotplug tran; do
    [ "${name}" = "${ROOT_DISK}" ] && continue
    if [ "${rm}" = "1" ] || [ "${hotplug}" = "1" ] || [ "${tran}" = "usb" ] || [ "${tran}" = "mmc" ] || [[ "${name}" == mmcblk* ]]; then
        CANDIDATES+=("${name}")
    fi
done < <(lsblk -dn -e7 -o NAME,RM,HOTPLUG,TRAN)

if [ ${#CANDIDATES[@]} -eq 0 ]; then
    echo "No SD card or USB disk found. Insert the card and try again."
    exit 1
fi

echo "SD cards / removable disks:"
echo ""
i=1
for dev in "${CANDIDATES[@]}"; do
    size="$(lsblk -dn -o SIZE "/dev/${dev}" | xargs)"
    model="$(lsblk -dn -o MODEL "/dev/${dev}" | xargs)"
    tran="$(lsblk -dn -o TRAN "/dev/${dev}" | xargs)"
    parts="$(lsblk -ln -o NAME,LABEL,MOUNTPOINTS "/dev/${dev}" | tail -n +2 \
             | awk '{l = ($2 == "" ? "-" : $2); m = ($3 == "" ? "" : " mounted at " $3); printf "%s%s(%s%s)", sep, $1, l, m; sep = " "}')"
    printf "  %d) /dev/%-10s %8s  %-5s %s\n" "${i}" "${dev}" "${size}" "${tran:-?}" "${model:-unknown}"
    [ -n "${parts}" ] && printf "       partitions: %s\n" "${parts}"
    i=$((i + 1))
done
echo ""

[ "${LIST_ONLY}" = "1" ] && exit 0

if [ ! -f "${IMAGE}" ]; then
    echo "Image not found: ${IMAGE}"
    echo "Build it first with ./docker-run.sh, or pass the image path as argument."
    exit 1
fi
IMAGE="$(realpath "${IMAGE}")"
BMAP="${IMAGE%.zst}"; BMAP="${BMAP}.bmap"

if [ -n "${M7_IMAGE}" ]; then
    echo "Boot container: ${IMAGE}"
    echo "Written to offset 32 KiB; partitions and data on the card are kept."
else
    echo "Image: ${IMAGE}"
fi
echo ""
read -r -p "Select disk [1-${#CANDIDATES[@]}] (or q to quit): " choice
[ "${choice}" = "q" ] && exit 0
if ! [[ "${choice}" =~ ^[0-9]+$ ]] || [ "${choice}" -lt 1 ] || [ "${choice}" -gt ${#CANDIDATES[@]} ]; then
    echo "Invalid choice"
    exit 1
fi
DEV="/dev/${CANDIDATES[$((choice - 1))]}"

# Image must fit on the card
DEV_BYTES="$(lsblk -dnb -o SIZE "${DEV}")"
IMG_BYTES=""
[ -f "${BMAP}" ] && IMG_BYTES="$(sed -n 's:.*<ImageSize> *\([0-9]*\) *</ImageSize>.*:\1:p' "${BMAP}")"
if [ -n "${IMG_BYTES}" ] && [ "${IMG_BYTES}" -gt "${DEV_BYTES}" ]; then
    echo "Image ($((IMG_BYTES / 1024 / 1024)) MB) is larger than ${DEV} ($((DEV_BYTES / 1024 / 1024)) MB)"
    exit 1
fi

echo ""
if [ -n "${M7_IMAGE}" ]; then
    echo "WARNING: the boot container on ${DEV} ($(lsblk -dn -o SIZE,MODEL "${DEV}" | xargs)) will be replaced."
    echo "         The card must already hold an epp-v2 image (flash it first without --m7)."
else
    echo "WARNING: everything on ${DEV} ($(lsblk -dn -o SIZE,MODEL "${DEV}" | xargs)) will be erased."
fi
read -r -p "Type the device name (${DEV##*/}) to confirm: " confirm
if [ "${confirm}" != "${DEV##*/}" ]; then
    echo "Aborted"
    exit 1
fi

# Unmount any mounted partitions of the card
for part in $(lsblk -ln -o NAME,MOUNTPOINTS "${DEV}" | awk 'NF > 1 {print $1}'); do
    echo "Unmounting /dev/${part}"
    sudo umount "/dev/${part}"
done

echo ""
echo "Flashing ${IMAGE##*/} to ${DEV} ..."
START=$(date +%s)
if [ -n "${M7_IMAGE}" ]; then
    # Same offset as the wic layout (IMX_BOOT_SEEK = 32)
    sudo dd if="${IMAGE}" of="${DEV}" bs=1k seek=32 conv=fsync status=progress
elif command -v bmaptool > /dev/null && [ -f "${BMAP}" ]; then
    sudo bmaptool copy --bmap "${BMAP}" "${IMAGE}" "${DEV}"
else
    zstd -dc "${IMAGE}" | sudo dd of="${DEV}" bs=4M conv=fsync status=progress
fi
sync
END=$(date +%s)

echo ""
echo "✅ Done in $((END - START)) s. ${DEV} can be removed."
echo "Insert the card in the FRDM-IMX95, set the boot switches to SD boot and power on."
