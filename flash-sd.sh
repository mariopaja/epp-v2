#!/bin/bash
# Flash the epp-v2 image to an SD card.
# Lists removable / USB / SD-reader disks, asks which one to use and writes the
# image with bmaptool (falls back to dd).
#
# Usage: ./flash-sd.sh [IMAGE.wic.zst]   # flash (default: latest build in images/)
#        ./flash-sd.sh --list            # only list candidate disks
set -e
source "$(dirname "${BASH_SOURCE[0]}")/config.sh"

LIST_ONLY=0
IMAGE=""
case "$1" in
    -l|--list) LIST_ONLY=1 ;;
    -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    "") IMAGE="${EPP_DIR}/images/epp-v2-image-${MACHINE}.rootfs.wic.zst" ;;
    *) IMAGE="$1" ;;
esac

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

echo "Image: ${IMAGE}"
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
echo "WARNING: everything on ${DEV} ($(lsblk -dn -o SIZE,MODEL "${DEV}" | xargs)) will be erased."
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
if command -v bmaptool > /dev/null && [ -f "${BMAP}" ]; then
    sudo bmaptool copy --bmap "${BMAP}" "${IMAGE}" "${DEV}"
else
    zstd -dc "${IMAGE}" | sudo dd of="${DEV}" bs=4M conv=fsync status=progress
fi
sync
END=$(date +%s)

echo ""
echo "✅ Done in $((END - START)) s. ${DEV} can be removed."
echo "Insert the card in the FRDM-IMX95, set the boot switches to SD boot and power on."
