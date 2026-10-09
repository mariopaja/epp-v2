# Zephyr board: frdm_imx95

Out-of-tree Zephyr board for the **NXP FRDM-IMX95** (i.MX 95 15x15,
LPDDR4x), the hardware epp-v2 is based on. Upstream Zephyr only has the EVKs
(`imx95_evk`, `imx95_evk_15x15`); this board is derived from
`imx95_evk_15x15`, which has the same SoC package, with the FRDM pinout taken
from the Linux device tree `imx95-15x15-frdm.dts` and the MCUXpresso SDK board
`frdmimx95`.

| Target | Core | Console |
|---|---|---|
| `frdm_imx95/mimx9596/m7` | Cortex-M7, runs from TCM | LPUART3 (USB debug port, channel A) |
| `frdm_imx95/mimx9596/a55` | one Cortex-A55, bare metal (no Linux) | LPUART1 (USB debug port) |

Enabled: console UART, eDMA2, LPTMR1 and TSTMR2 (M7). Not yet enabled:
Ethernet, CAN, I2C, SPI, GPIO expander; pin groups for CAN2 are prepared in
`frdm_imx95-pinctrl.dtsi`.

**Status:** builds without warnings (Zephyr main of 2026-10-04, Zephyr SDK
1.0.1) for `samples/hello_world`, `samples/synchronization` and
`samples/subsys/shell/shell_module` on both targets. Tested on hardware:
`samples/synchronization` on the M7, started from the boot container
(`EPP_M7_IMAGE`). The A55 target is not yet tested on hardware. No LEDs are
defined, so `samples/basic/blinky` does not build.

## Build

Inside a Zephyr workspace (`west init` / `west update`, Zephyr SDK installed):

```bash
west build -p -b frdm_imx95/mimx9596/m7 samples/hello_world \
    -- -DBOARD_ROOT=$HOME/dev/epp-v2/zephyr
```

Or add this directory as a Zephyr module (`zephyr/module.yml` sets the board
root) with `-DEXTRA_ZEPHYR_MODULES=$HOME/dev/epp-v2/zephyr`.

## Run on the M7

The M7 firmware is part of the **boot container** (`imx-boot`): the System
Manager on the Cortex-M33 starts it at power-on, before U-Boot, Linux or Xen.
No remoteproc and no U-Boot commands are needed.

1. Build Zephyr and copy the raw image (`zephyr.bin`, not `zephyr.elf`) into
   the epp-v2 repo (the build container only sees this directory):

   ```bash
   west build -p -b frdm_imx95/mimx9596/m7 samples/synchronization \
       -- -DBOARD_ROOT=$HOME/dev/epp-v2/zephyr
   cp build/zephyr/zephyr.bin $HOME/dev/epp-v2/m7/zephyr.bin
   ```

2. Point `EPP_M7_IMAGE` in [board.conf](../board.conf) at it:

   ```bash
   EPP_M7_IMAGE="m7/zephyr.bin"
   ```

3. Rebuild and flash: `./docker-run.sh` (or with `--xen`), then
   `./flash-sd.sh`.

The image ends up as `m7_image.bin` in the boot container, in the raw area at
the start of the SD card, not in a partition:

```
0x000000  MBR / partition table
0x008000  imx-boot (boot container), read by the Boot ROM
            ELE firmware, M33 OEI (DDR) + System Manager,
            M7 image (zephyr.bin), A55 SPL / TF-A / U-Boot
0x800000  p1 boot (FAT), p2 rootfs, ...
```

The Boot ROM loads the M7 image to the M7 ITCM (load address `0x303c0000`,
address `0x0` for the M7) and the System Manager starts it. Its offset inside
the container is chosen by `imx-mkimage` and can change, so always write the
whole container. To update the container from Linux on the board instead:

```bash
dd if=imx-boot.tagged of=/dev/mmcblk1 bs=1k seek=32 conv=fsync   # then reboot
```

If that write fails the board does not boot from the card; reflash it with
`./flash-sd.sh`.

With `EPP_M7_IMAGE` empty, the container holds NXP's demo
`imx95-15x15-frdm_m7_TCM_power_mode_switch.bin` instead. With
`EPP_M7_IMAGE="none"` it holds no M7 image at all (container `flash_a55`) and
the M7 stays off.

**Update only the M7 firmware** on a card that already holds an epp-v2 image,
without rebuilding or reflashing the rest:

```bash
./flash-sd.sh --m7 ~/dev/zephyrproject/zephyr/build/zephyr/zephyr.bin
./flash-sd.sh --m7 none        # remove the M7 image, M7 stays off
```

It copies the file to `m7/` (if it is outside this repo), rebuilds only
`imx-boot` with it (seconds when cached), and writes only the boot container
to offset 32 KiB of the selected card; partitions and data are kept. This
overrides `EPP_M7_IMAGE` from `board.conf` for that run only, so set it there
too if the next full build should keep it.

**Linux and the M7:** Linux attaches to the running M7 (`remoteproc1`,
`state=attached`, `lmm(1) not under Linux Control` in `dmesg`). It cannot stop
it or load other firmware, because the System Manager started it. Zephyr
images without a resource table can make remoteproc print a warning; the M7
keeps running.

**M7 console:** LPUART3, on the third of the four USB serial ports of the
debug USB (115200 8N1; `/dev/ttyACM2` when the board is the only such device,
`ttyACM0` is the A55 console). On the FRDM it reaches the USB port only while
line 3 ("UART3/GPIO select") of the PCAL6524 GPIO expander (LPI2C2, address
0x22) is low. Linux drives it low with a gpio-hog (`lpuart-gpio-sel-hog` in
`/sys/kernel/debug/gpio`), so M7 output appears once Linux has booted
(10-15 s after power-on); anything printed before is lost.
For a first test use a sample that prints repeatedly, such as
`samples/synchronization`. The M7 does not touch LPI2C2, which belongs to the
A55 side.

## Zephyr as a Xen DomU

Not the `frdm_imx95` board: a Zephyr guest under Xen runs on the virtual
machine Xen provides, so it is built for Zephyr's generic
`xenvm/xenvm/gicv3` board (guest RAM at `0x40000000`, GICv3). For a dom0less
DomU on this board, [xen/xenvm-dom0less.overlay](xen/xenvm-dom0less.overlay)
and [xen/xenvm-dom0less.conf](xen/xenvm-dom0less.conf) switch the console to
the PL011 Xen emulates for the domain (`0x22000000`) instead of the Xen PV
console of `xenvm`, and add the arm64 image header Xen needs to load the
kernel:

```bash
west build -p -b xenvm/xenvm/gicv3 samples/synchronization -- \
    -DEXTRA_DTC_OVERLAY_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.overlay \
    -DEXTRA_CONF_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.conf
cp build/zephyr/zephyr.bin $HOME/dev/epp-v2/xen/zephyr.bin
```

Then set `EPP_XEN_ZEPHYR_IMAGE="xen/zephyr.bin"` in
[board.conf](../board.conf), build with `./docker-run.sh --xen` and flash.
`boot.scr` creates the domain `/chosen/domU2` (`EPP_XEN_ZEPHYR_VCPUS`,
`EPP_XEN_ZEPHYR_MEM`; the memory must cover the 16 MB RAM of `xenvm`) with
`xen,enhanced`, so the domain gets xenstore once Dom0 has booted. Its
output appears on the Xen serial console (`ttyACM0`); Ctrl-a three times
moves the input to DOM2.

The Zephyr DomU gets its own physical core by default
(`EPP_XEN_ZEPHYR_CPU="5"`): Xen puts it in a cpupool with the `null`
scheduler, so its vCPU is never moved or shared with Dom0 or the Linux DomU.

To replace only the Zephyr DomU on a running board, copy the new
`zephyr.bin` to the boot partition as `zephyr-domu.bin` (from Dom0:
`mount /dev/mmcblk1p1 /mnt`) and restart the board.

## Network for the Zephyr DomU (Xen netfront)

Zephyr has no Xen PV drivers of its own, so this directory is also a Zephyr
module with a xenstore client and a Xen network frontend (netfront). The
Zephyr DomU then shares the board's Ethernet port with Dom0 and the Linux
DomU through the bridge `xenbr0` in Dom0, with its own MAC (`00:16:3e:...`,
from Xen) and an address from DHCP:

```
LAN -- eth0 -- Dom0: xenbr0 --+-- Dom0 (epp-v2-dom0)
                              +-- vif1.0 -- Linux DomU (epp-v2)
                              +-- vif2.0 -- Zephyr DomU (netfront)
```

| Kconfig | Function |
|---|---|
| `CONFIG_EPP_XEN_XENSTORE` | xenstore client (`epp/xen/xenstore.h`: `epp_xs_init`, `epp_xs_read`, `epp_xs_write`, `epp_xs_directory`). Waits until Dom0 (`init-dom0less`) has set up the domain's xenstore |
| `CONFIG_EPP_XEN_NETFRONT` | Ethernet interface on `device/vif/0`: waits for the vif Dom0 attaches (`xl network-attach zephyr bridge=xenbr0 type=vif` in `epp-v2-dom0less-setup`), grants the rings and buffers to Dom0 and connects. `_RX_BUFS` / `_TX_BUFS` set the number of 4 KiB buffer pages (default 32 / 16) |

Build with the module (`EXTRA_ZEPHYR_MODULES`), for example the sample
[samples/xen_netfront](samples/xen_netfront), which gets an address by DHCP
and prints the state every 5 s:

```bash
west build -p -b xenvm/xenvm/gicv3 $HOME/dev/epp-v2/zephyr/samples/xen_netfront -- \
    -DEXTRA_ZEPHYR_MODULES=$HOME/dev/epp-v2/zephyr \
    -DEXTRA_DTC_OVERLAY_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.overlay \
    -DEXTRA_CONF_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.conf
```

```
<inf> epp_xenstore: xenstore connected (pfn 0x39001, evtchn 1)
<inf> epp_netfront: vif backend /local/domain/0/backend/vif/2/0 (dom 0), mac 00:16:3e:..
<inf> epp_netfront: connected: tx-ring-ref 8, rx-ring-ref 9, evtchn 2
<inf> net_dhcpv4: Received: 10.10.193.193
```

[samples/xen_xenstore](samples/xen_xenstore) only reads the domain's own
xenstore nodes.

The driver reconnects by itself: when the backend leaves Connected (for
example `xl network-detach zephyr 0` in Dom0) it switches the carrier off, closes
its side so `xl` finishes at once, and waits for a new vif
(`xl network-attach zephyr bridge=xenbr0 type=vif`), which may come with a new MAC;
DHCP then gets a new address. The ring and buffer pages are granted once and
the grant references reused, because Zephyr's `gnttab_end_access()` has its
check inverted (it frees grants that are still in use and keeps released
ones).

Limitations: one queue, one page per packet (no scatter-gather, no
checksum/GSO offloads, MTU 1500), receive in rx-copy mode, the backend must
stay in the same domain (Dom0). `xenvm` has no entropy source; the sample uses
`CONFIG_TEST_RANDOM_GENERATOR`, which is fine for DHCP but not for TLS or
other cryptography.

Tested on hardware: DHCP and ping from the LAN to the Zephyr DomU, next to
the Linux DomU on the same port and Zephyr on the M7; four detach/attach
cycles in a row, each reconnecting with a new MAC and address.
