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

## Hardware passthrough to the Zephyr DomU

Xen gives devices to the Zephyr DomU, mapped at their physical addresses;
Dom0 then no longer sees them (`status = "disabled"` in its device tree).
GPIO2 with the RGB LED is always passed through, plus any combination of
the options, selected on the board:

| Name | Device | Address | On the FRDM-IMX95 | Zephyr compatible |
|---|---|---|---|---|
| `gpio2` (always) | GPIO2 | `0x43810000` | RGB LED D19 (green GPIO_IO04, blue GPIO_IO12, red GPIO_IO13, active high), GPIO_IOxx pins of the 40-pin header | `epp,xen-rgpio` |
| `lpi2c4` (option) | LPI2C4 (Linux `i2c-3`) | `0x42540000` | display bus: IT6263 LVDS-to-HDMI bridge (0x4c, 0x33), PCA9632 backlight LED driver (0x62); Linux loses HDMI | `epp,xen-lpi2c` |
| `lpi2c5` (option) | LPI2C5 (disabled in Linux) | `0x426b0000` | I2C of the 40-pin header J19: pin 27 SDA (GPIO_IO00), pin 28 SCL (GPIO_IO01) | `epp,xen-lpi2c` |
| `lpi2c6` (option) | LPI2C6 (disabled in Linux) | `0x426c0000` | I2C of the 40-pin header J19: pin 3 SDA (GPIO_IO02), pin 5 SCL (GPIO_IO03) | `epp,xen-lpi2c` |
| `lpi2c7` (option) | LPI2C7 (disabled in Linux) | `0x426d0000` | I2C of the 40-pin header J19: pin 31 SDA (GPIO_IO06), pin 26 SCL (GPIO_IO07) | `epp,xen-lpi2c` |
| `lpspi7` (option) | LPSPI7 (disabled in Linux) | `0x42710000` | SPI of the 40-pin header J19: SCK pin 26, MOSI pin 31, MISO pin 29, chip select pin 35 (GPIO2 19), reset pin 37 (GPIO2 26) | `epp,xen-lpspi` |
| `flexcan2` (option) | FlexCAN2 (disabled in Linux, also without passthrough) | `0x425b0000`, interrupt GIC SPI 38 | CAN2, transceiver TJA1051, connector J20 (pin 1 CANH, 2 CANL, 3 GND); Linux keeps CAN5 (J21) | `epp,xen-flexcan` |
| `lpuart7` (option) | LPUART7 (disabled in Linux) | `0x42690000` | UART of the 40-pin header J19: pin 24 TX (GPIO_IO08), pin 21 RX (GPIO_IO09); Dom0 then switches U16 to the header and leaves LPSPI3 (M.2 slot SPI) off | `epp,xen-lpuart` |

Any combination works, except `lpi2c7` with `lpspi7`: both use pins 31/26
(`EPP_XEN_ZEPHYR_PT_CONFLICTS` in layer.conf). `epp-passthrough` refuses to
select both.

### Selecting the passthrough

In Dom0 (SSH or serial console), `epp-passthrough` shows the options; a
number ticks or unticks one, `s` saves the selection for the next boot:

```
root@epp-v2-dom0:~# epp-passthrough
Xen passthrough to the Zephyr DomU

         always  gpio2    GPIO2: RGB LED D19, GPIO_IOxx pins of the 40-pin header

  1  [ ]          lpi2c4   LPI2C4: display I2C bus (IT6263 HDMI bridge, PCA9632), Linux loses HDMI
  2  [x]          lpi2c5   LPI2C5: I2C of the 40-pin header, pin 27 SDA, pin 28 SCL
  3  [x]          lpi2c6   LPI2C6: I2C of the 40-pin header, pin 3 SDA, pin 5 SCL
  4  [ ]          lpi2c7   LPI2C7: I2C of the 40-pin header, pin 31 SDA, pin 26 SCL

  running now: gpio2,lpi2c6

Toggle 1-4, s to save, q to quit:
```

```bash
epp-passthrough lpi2c5 lpi2c6    # select without the menu
epp-passthrough none
epp-passthrough -l               # show options, running and next selection
reboot
```

It writes `epp-passthrough.txt` (`epp_zephyr_pt=lpi2c5_lpi2c6`: the
options in layer.conf order, joined with `_`) to the boot partition.
Without that file the build default applies, `EPP_XEN_ZEPHYR_PASSTHROUGH`
in [board.conf](../board.conf) (space separated; empty: none). At the U-Boot
prompt, `setenv epp_zephyr_pt lpi2c4_lpi2c6; boot` overrides both for one
boot. No rebuild is needed to switch.

### How it works

Nothing is passed through automatically, and Xen hands over whole register
blocks, not single pins or bus addresses: the DomU gets all 32 pins of
GPIO2, or the whole I2C bus with every device on it. The build prepares
every combination of the options that does not contain a conflicting pair
(96 for the seven options); `boot.scr` picks the selected one:

1. **Xen:** `boot.scr` marks the host nodes `xen,passthrough`, so Xen takes
   them from Dom0, and loads the partial device tree
   `xen-pt/epp-zephyr-pt-<combination>.dtb` as `multiboot,device-tree` module of
   `/chosen/domU2`, so Xen maps their registers into the DomU (`xen,path`:
   host node, `xen,reg`: address mapping, 1:1,
   `xen,force-assign-without-iommu`).
2. **Pin mux:** Linux no longer sets the pins of a device it does not own.
   `boot.scr` applies `overlays/epp-zephyr-pt-pins-<combination>.dtbo`, which
   sets them once at boot instead (SCMI pinctrl hog of Dom0): for `gpio2`
   the three LED pads, for `lpi2c4` GPIO_IO30/31, for `lpi2c5` GPIO_IO00/01,
   for `lpi2c6` GPIO_IO02/03, for `lpi2c7` GPIO_IO06/07, for `lpspi7`
   GPIO_IO05-07 and GPIO_IO16/19/26 (as GPIO). To
   use other GPIO2 pins of the 40-pin header, add their
   `IMX95_PAD_GPIO_IOxx__GPIO2_IO_BITxx` entries to
   `epp-zephyr-gpio2-pins.dtsi` and rebuild the image.
3. **Zephyr:** `boot.scr` passes the list to Dom0 (`epp.passthrough=gpio2,lpi2c6`
   on its command line); `epp-v2-dom0less-setup` writes it to the Zephyr
   DomU's xenstore (`epp/passthrough`, readable only by that domain). Zephyr
   does not discover devices from Xen: the application has nodes for all of
   them, and starts an I2C bus (`zephyr,deferred-init`, `device_init()`) only
   when it is in that list. Touching a device that is not passed through
   aborts.

The devices are defined once, in [layer.conf](../meta-epp-v2/conf/layer.conf)
(`EPP_XEN_ZEPHYR_PT_FIXED`, `EPP_XEN_ZEPHYR_PT_OPTIONS`, and per device
`EPP_XEN_ZEPHYR_PT_NODE[name]` and `EPP_XEN_ZEPHYR_PT_DESC[name]`), plus one
fragment pair each in `meta-epp-v2/recipes-bsp/epp-v2-overlays/files/`:

| Fragment | Content |
|---|---|
| `epp-zephyr-<name>-pt.dtsi` | node of the partial device tree: `xen,path`, `xen,reg`, `xen,force-assign-without-iommu` |
| `epp-zephyr-<name>-pins.dtsi` | pin group `pinctrl_epp_<name>` for the hog |
| `epp-zephyr-<name>-dom0.dtsi` | optional: other changes to Dom0's device tree while the device is passed through (top level of the pin mux overlay); `flexcan2` keeps the CAN transceivers on |
| `epp-zephyr-dom0.dtsi` | changes to Dom0's device tree in every combination: FlexCAN2 off (reserved for the Zephyr DomU) |

From these the build generates the files per combination (`epp-v2-overlays`,
combinations: `EPP_XEN_ZEPHYR_PT_COMBOS`), the combination table in
`boot.scr` (`epp-v2-bootscript`) and the list of `epp-passthrough`
(`epp-v2-xen-dom0`). Options that share pins or controllers go into
`EPP_XEN_ZEPHYR_PT_CONFLICTS` (`a:b` pairs); the build leaves out their
combinations and stops if the default selects both. Each option doubles the
number of combinations.

**Adding a device:** write the two fragments, add it to layer.conf and to the
check in `scripts/yocto-build.sh`, and give Zephyr a driver and a node for
it. Only devices without DMA work: the SMMU is off for Xen, so a
passed-through device that does DMA could write to any memory. The I2C and
SPI controllers here are polled; their interrupts are not passed through.

**Interrupts:** a device's interrupt goes to the DomU when its fragment has
`interrupts` with `interrupt-parent = <0xfde8>` (65000, Xen's phandle for
the guest GIC); Xen routes the physical SPI to the DomU under the same
number, and Dom0 no longer gets it. `gpio2` does this for its first
interrupt (GIC SPI 49), so Zephyr gets GPIO pin interrupts; the Zephyr node
needs the same interrupt (`interrupts = <GIC_SPI 49 IRQ_TYPE_LEVEL
IRQ_DEFAULT_PRIORITY>`).

Clock and power stay with the System Manager: the GPIO2, LPI2C4 and LPI2C6
clocks are on from boot (LPI2C: 24 MHz, hardware enable `Y` in
`/sys/kernel/debug/clk/clk_summary` in Dom0, also for LPI2C6, which Linux
never uses), and Dom0 boots with `clk_ignore_unused`. The System Manager
configuration (`mx95evk.cfg`) gives these controllers and their pads to the
A55 side (`LM2 "AP"`), not to the M7. The board's "PWM/GPIO select" (LED
instead of PWM) is a gpio-hog in the FRDM device tree, set by Dom0.

### I2C on the 40-pin header (`lpi2c5`, `lpi2c6`, `lpi2c7`)

Three I2C buses of the header J19 (schematic page 30) are usable, all with
pins that go directly to the SoC and controllers nothing else on the board
uses:

| Option | SDA | SCL | Note |
|---|---|---|---|
| `lpi2c6` | pin 3 (GPIO_IO02) | pin 5 (GPIO_IO03) | the usual I2C pins of a Raspberry Pi header |
| `lpi2c5` | pin 27 (GPIO_IO00) | pin 28 (GPIO_IO01) | ID_SD / ID_SC on a Raspberry Pi: a HAT's ID EEPROM appears here |
| `lpi2c7` | pin 31 (GPIO_IO06) | pin 26 (GPIO_IO07) | |

Power: pin 1 (and 17) 3.3 V, pin 6 (and 9, 14, 20, 25, 30, 34, 39) GND.

Not usable: the same pins as LPI2C4 (pins 3/5) or LPI2C3 (pins 27/28),
because those controllers already serve the display and USB-C buses on
other pins and each I2C signal has one input select; LPI2C6 on pins 7/29
(pin 7 is the green LED); LPI2C7/8 on pins 21/24 and 19/23, and LPI2C5 on
pins 15/16, which go through the board's select switches (schematic page
18; the SPI pins there showed no signal in an earlier test); LPI2C8 on pins
32/33 (blue and red LED).

- **No pull-ups on the board** (R660-R663 are not fitted for pins 3/5 and
  27/28, none for pins 31/26). The connected
  module needs its own (most breakout boards have them), or add about
  2.2-4.7 kOhm from SDA and SCL to 3.3 V. The pads have the SoC's weak
  internal pull-ups enabled, enough for an idle bus but not for reliable
  transfers.
- **3.3 V only.** Power modules with pull-ups from pin 1, never from 5 V: the
  pull-ups would put 5 V on the SoC pins.
- **Start-up timing:** Xen starts the Zephyr DomU together with Dom0, but the
  header pins are switched to I2C by Dom0's pinctrl hog only when its Linux
  boots, about 5 s later. Until then every transfer fails (`-EIO`). The
  application starts the bus only after Dom0 has written the passthrough
  list, so this is already over; other code must retry its first accesses.

### SPI on the 40-pin header (`lpspi7`)

| J19 pin | Signal |
|---|---|
| 26 | SCK (GPIO_IO07) |
| 31 | MOSI / SOUT (GPIO_IO06) |
| 29 | MISO / SIN (GPIO_IO05) |
| 35 | chip select (GPIO_IO19, GPIO2 19, active low) |
| 37 | device reset (GPIO_IO26, GPIO2 26, active low), optional |
| 36 | device interrupt (GPIO_IO16, GPIO2 16, input with pull-up), optional |
| 17 / 20 | 3.3 V / GND |

LPSPI7's own chip select (PCS0) is on GPIO_IO04, the green LED, so the
chip select is a GPIO2 pin driven by the driver (`cs-gpios`). Other SPI
pins of the header: LPSPI6 (pins 5/3/28/27) uses the pins of `lpi2c6` and
`lpi2c5`; LPSPI3 (pins 23/19/21/24) goes through the board's analog switch
U16 (schematic page 18), which connects the pins to the M.2 Wi-Fi slot
unless "SPI3/GPIO select" of the GPIO expander is high, and Linux in Dom0
uses LPSPI3 and these pads for the M.2 slot (see `lpuart7`).

`xen_demo` tests the bus every 10 s: without a device in a
loopback test (jumper from MOSI pin 31 to MISO pin 29, no chip select);
with `extras/w5500.overlay` + `extras/w5500.conf` a WIZnet W5500 Ethernet module (SCLK
26, MOSI 31, MISO 29, SCSn 35, RSTn 37, INTn not connected): its version,
PHY state and a MAC address register write and read back:

```
app: header spi: W5500 version 0x04 OK, link down, MAC write/read OK
```

This is a register test with its own compatible (`epp,w5500-test`), so that
no driver binds to the chip.

**W5500 as a network interface:** with `extras/w5500-eth.overlay` +
`extras/w5500-eth.conf` (preset `header-spi-w5500-eth`) the node is a real
`wiznet,w5500` with Zephyr's W5500 Ethernet driver (random MAC address):
a second network interface next to netfront, with its own Ethernet port and
DHCP. Its INTn goes to pin 36 (`int-gpios = <&gpio2 16
GPIO_ACTIVE_LOW>`): the driver waits for the GPIO interrupt instead of
polling the chip. Without that wire, remove `int-gpios` and the driver
polls (log: "polling mode"; ping about 7 ms instead of 0.8 ms):

```
app: net: carrier on, mac 00:16:3e:12:ea:d9, ipv4 10.10.193.32
app: w5500: carrier on, mac fa:7b:82:00:db:2b, ipv4 10.10.193.243
```

Zephyr's network stack and this driver access the chip at boot, so two
things change compared with the other parts:

- **LPSPI7 starts at boot**, not deferred: the image needs `lpspi7` passed
  through. Without it (unticked in `epp-passthrough`) Zephyr aborts at its
  first SPI access.
- **U-Boot sets the header pins.** Dom0 switches them only about 5 s after
  Xen has started the Zephyr DomU. `EPP_XEN_ZEPHYR_EARLY_PINS="lpspi7"` in
  [board.conf](../board.conf) (preset: `EARLY_PINS`) adds the pin groups of
  these devices (the same `epp-zephyr-<name>-pins.dtsi` fragments) to the
  pinctrl hog of U-Boot's device tree, which U-Boot applies over SCMI before
  it starts Xen. This changes U-Boot in the boot container: flash the whole
  card (`./flash-sd.sh`). The pins then stay switched for every passthrough
  selection.

### CAN between the Zephyr DomU and Linux (`flexcan2`)

The board has three CAN transceivers (TJA1051, schematic page 31) with
termination: CAN2 on connector J20 and CAN5 on J21 (PHB 2.0 mm, pin 1
CANH, 2 CANL, 3 GND; CAN1 goes to the connector of page 19). With
`flexcan2` the Zephyr DomU gets FlexCAN2 and its interrupt; Linux in Dom0
keeps FlexCAN5. Connect J20 to J21 (1-1, 2-2, 3-3) for a bus between the
two:

```bash
# Dom0 (iproute2 and can-utils are in the Dom0 image)
ip link set can0 type can bitrate 500000
ip link set can0 up
candump -t d can0 &
cansend can0 100#11223344
```

```
 (000.000000)  can0  123   [4]  00 00 00 66      Zephyr heartbeat, every second
 (000.422557)  can0  100   [4]  11 22 33 44      cansend in Dom0
 (000.000170)  can0  101   [4]  11 22 33 44      Zephyr's answer
app: can (J20): error-active (tx err 0, rx err 0), sent 21, received 1, last 0x100 [4] 11 22 33 44
```

- **Bit rate:** 500 kbit/s from the CAN2 clock root, 24 MHz (the System
  Manager's default; Linux sets 40 MHz only while it owns the controller):
  prescaler 3, 16 time quanta, sample point 87.5 %.
- **Transceiver standby:** the standby line (`CAN_STBY`, GPIO expander
  line 7, regulator `can-stby` in Linux) is shared by the three
  transceivers, and Linux switches it on only while one of its CAN
  interfaces is up. `epp-zephyr-flexcan2-dom0.dtsi` makes it
  `regulator-always-on` while `flexcan2` is passed through. Without it the
  Zephyr DomU's frames do not reach the bus (bit errors, bus-off) while
  Linux's `can0` is down.
- **J20 reserved for the Zephyr DomU:** FlexCAN2 is disabled in Dom0 in
  every combination (`epp-zephyr-dom0.dtsi`), also when `flexcan2` is not
  passed through. When Linux owned it, its runtime power management turned
  the CAN2 clock off through the System Manager; that survives a warm
  reboot, and FlexCAN2 then stayed in low-power mode in the next boot that
  passed it through (driver: "no response (MCR ...)") until a power cycle.
  After updating from an image without this fragment, power cycle once.
- **CAN2/CAN5 switch:** the pins reach the transceivers through U19
  (schematic page 18) while "CAN2&5/GPIO select" is low, a gpio-hog of the
  FRDM device tree that Dom0 sets independently of FlexCAN2.
- **No other node:** without a node that acknowledges (Linux's `can0`
  down, nothing wired) the Zephyr DomU's frames are retransmitted and it
  goes error-passive; it recovers by itself once another node is on the bus
  (also from bus-off). The application sends with a callback, so it never
  waits for that.

Without wiring: `extras/can-loopback.conf` (preset `can-loopback`) starts FlexCAN2
in loopback mode, where it receives its own frames internally; every 10 s
the application also sends a request to itself:

```
app: can (J20, loopback): error-active (tx err 0, rx err 0), sent 23, received 23, last 0x100 [2] ca fe
```

### UART on the 40-pin header (`lpuart7`)

| J19 pin | Signal |
|---|---|
| 24 | TX (GPIO_IO08) |
| 21 | RX (GPIO_IO09) |

The pins go through the board's analog switch U16 (schematic page 18): with
"SPI3/GPIO select" (GPIO expander line 2) low they connect to the M.2
slot's SPI, high to the header. Two settings of the FRDM device tree in
Dom0 work against that, and `epp-zephyr-lpuart7-dom0.dtsi` changes both
while `lpuart7` is passed through:

- the gpio-hog of "SPI3/GPIO select" drives it low (`output-low`). An
  overlay cannot delete that property, so the hog becomes
  `GPIO_ACTIVE_LOW`: the same output-low now drives the line high.
- LPSPI3 (`spidev` for the M.2 slot) is enabled, and its pin group claims
  GPIO_IO08-11 (GPIO_IO08 as its chip select). Linux applies it after the
  pinctrl hog and takes the UART pads back, so LPSPI3 is disabled in Dom0.

The M.2 slot's SPI is unusable meanwhile (the Wi-Fi/BT module uses SDIO and
LPUART5).

`xen_demo` tests the UART every 10 s in a loopback (wire from
pin 24 to pin 21, which are diagonal, not neighbours): it sends a line at
115200 baud byte by byte and expects every byte back:

```
app: header uart (pins 24/21): loopback OK (15 bytes, 115200 baud)
```

The binding's `internal-loopback` property loops TX to RX inside the LPUART
instead (CTRL[LOOPS]), to test the driver without the pin path.

### Drivers

The upstream NXP drivers need SCMI (clocks), pinctrl and the MCUX SoC
headers, which a `xenvm` DomU does not have, so this module has minimal
drivers:

| Kconfig | Driver |
|---|---|
| `CONFIG_EPP_XEN_RGPIO` | RGPIO, `epp,xen-rgpio`: inputs and outputs; pin interrupts (rising, falling, both edges, low and high level, through the interrupt control registers ICR and the flags in ISFR) when the node has the interrupt Xen routes; no pull or open-drain flags (pad settings belong to the pin mux). The secure-only registers PCNS/PCNP must not be accessed from the DomU; reading them aborts |
| `CONFIG_EPP_XEN_LPI2C` | LPI2C controller, `epp,xen-lpi2c`: 7-bit addresses, standard (100 kHz) and fast mode (400 kHz), polled. The SCL timing assumes the 24 MHz functional clock (`functional-clock-frequency`) |
| `CONFIG_EPP_XEN_LPUART` | LPUART, `epp,xen-lpuart`: polled (`poll_in`, `poll_out`, `err_check`), 8N1, baud rate from `current-speed` (oversampling and divider with the smallest error from the 24 MHz clock), optional `internal-loopback` |
| `CONFIG_EPP_XEN_FLEXCAN` | FlexCAN, `epp,xen-flexcan`: classic CAN (no CAN FD), standard and extended IDs, remote frames, 8 receive filters (one mailbox each, individual masks), one transmit mailbox, normal, loopback and listen-only mode, state changes; interrupt driven. Start-up and i.MX95 quirks as in Linux's flexcan driver (ECC RAM init, MECR, reserved mailbox 0). Bit timing from the 24 MHz CAN clock (`functional-clock-frequency`) |
| `CONFIG_EPP_XEN_LPSPI` | LPSPI controller, `epp,xen-lpspi`: controller mode, 8-bit words, full duplex, SPI modes 0-3, MSB or LSB first, polled, chip select by GPIO (`cs-gpios`). SCK = 24 MHz / (2^prescale x (div + 2)), up to 12 MHz |

### The demo application (`samples/xen_demo`)

One Zephyr image for every passthrough selection. The LED D19 on GPIO2
(always passed through) cycles red, green, blue, white, off; netfront gets
an address by DHCP. Once Dom0 has booted, the application reads the
passthrough list from xenstore (`epp/passthrough`) and starts the **parts**
of the listed devices; every 10 s it prints the network state and every
part's result. Devices are `zephyr,deferred-init` in `app.overlay`: a part
starts its device only when it is passed through, so nothing touches a
device that is not.

```
samples/xen_demo/
  src/main.c          LED, netfront, passthrough list, runs the parts
  src/part.h          a part: passthrough option, start(), every_second(), report()
  src/i2c_scan.c      I2C bus scan, shared by the I2C parts
  src/i2c_display.c   part lpi2c4
  src/i2c_header.c    parts lpi2c5, lpi2c6, lpi2c7
  src/pcf8574.c       PCF8574 on lpi2c6 (extras)
  src/spi_loopback.c  part lpspi7: loopback test
  src/w5500_test.c    part lpspi7 with a W5500: register test (extras)
  src/w5500_eth.c     part lpspi7 with a W5500: network interface (extras)
  src/can.c           part flexcan2
  src/uart.c          part lpuart7
  app.overlay         GPIO2, the LED and all passthrough devices
  Kconfig, prj.conf   one option per part
  extras/             overlays and Kconfig fragments of the optional parts
  dts/bindings/       epp,w5500-test (register test without driver)
```

Each part file defines its parts with `DEMO_PART()` (an iterable section,
run in name order); `CMakeLists.txt` compiles a file only when its Kconfig
option is on. When Dom0 passes a device through whose part is not compiled
in, the application says so (`app: lpi2c4 is passed through, but its part
is not compiled in (Kconfig)`) and does not touch it.

| Part | Option | Kconfig (default) | Extras (`extras/`) | Every 10 s |
|---|---|---|---|---|
| display bus | `lpi2c4` | `CONFIG_EPP_LPI2C4` (off: the `lpi2c4` node is disabled, it belongs to HDMI) | `lpi2c4.overlay` | scan, IT6263 chip ID, PCA9632 register write and read back |
| header I2C | `lpi2c5`, `lpi2c6`, `lpi2c7` | `CONFIG_EPP_LPI2C5/6/7` (on) | | scan of pins 27/28, 3/5, 31/26 |
| PCF8574 | `lpi2c6` | `CONFIG_EPP_PCF8574` (off) | `pcf8574.overlay` + `.conf` | P0-P7 walk, steps and errors |
| header SPI | `lpspi7` | `CONFIG_EPP_LPSPI7` (on) | | loopback, jumper MOSI 31 to MISO 29 |
| W5500 test | `lpspi7` | `CONFIG_EPP_W5500` (off) | `w5500.overlay` + `.conf` | version, PHY state, MAC write and read back |
| W5500 Ethernet | `lpspi7` | `CONFIG_EPP_W5500_ETH` (off; LPSPI7 starts at boot) | `w5500-eth.overlay` + `.conf` | network state of the W5500 interface |
| CAN | `flexcan2` | `CONFIG_EPP_FLEXCAN2` (on), `CONFIG_EPP_FLEXCAN2_LOOPBACK` (off) | `can-loopback.conf` | state, counters, last frame; heartbeat 0x123 every second, 0x100 answered on 0x101 |
| header UART | `lpuart7` | `CONFIG_EPP_LPUART7` (on) | | loopback, wire TX 24 to RX 21 |

The options that follow a device node are on when the node in
`app.overlay` is `"okay"`. **Adding a part:** a node for the device in
`app.overlay` (`zephyr,deferred-init`), a Kconfig option, a file in `src/`
with `DEMO_PART(pN_<option>, .option = "<option>", .start = ..., .report =
...)`, and a `target_sources_ifdef()` line in `CMakeLists.txt`.

```bash
west build -p -b xenvm/xenvm/gicv3 $HOME/dev/epp-v2/zephyr/samples/xen_demo -- \
    -DEXTRA_ZEPHYR_MODULES=$HOME/dev/epp-v2/zephyr \
    -DEXTRA_DTC_OVERLAY_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.overlay \
    -DEXTRA_CONF_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.conf
```

Extras are added to the two lists, e.g. for the W5500 register test
`...;$HOME/dev/epp-v2/zephyr/samples/xen_demo/extras/w5500.overlay` and
`...;.../extras/w5500.conf`. Or with the Yocto image in one step, from a
preset: `./build-demo.sh header-i2c` (see the main
[README](../README.md#demo-presets-build-demosh)).

[samples/xen_netfront](samples/xen_netfront) and
[samples/xen_xenstore](samples/xen_xenstore) are the minimal examples of the
network frontend and the xenstore client.

Every 10 s, with `lpi2c5`, `lpi2c6` and `lpi2c7` selected (a PCF8574 on
pins 3/5):

```
app: [6] LED cycling, passthrough gpio2,lpi2c5,lpi2c6,lpi2c7
app: net: carrier on, mac 00:16:3e:60:27:07, ipv4 10.10.193.224
app: header i2c (J19 pins 27/28): no devices
app: header i2c (J19 pins 3/5): 0x20
app: header i2c (J19 pins 31/26): no devices
```

With `lpi2c4` (and `CONFIG_EPP_LPI2C4`):

```
app: display i2c: 0x33 0x4c 0x62
app: display i2c: IT6263 ID OK, PCA9632 write/read 0x5a OK
```

The PCA9632 may also answer at 0x70 (its "all call" address, on after a
power cycle until Linux's driver turns it off).

**PCF8574 test:** a PCF8574 I/O expander module (not on the board) on the
header at 0x20 (A0-A2 to GND; a PCF8574A is at 0x38), driven with Zephyr's
`nxp,pcf857x` GPIO driver (`extras/pcf8574.overlay` +
`extras/pcf8574.conf`, preset `header-pcf8574`). Its pins P0-P7 go high one
after the other, then all high, then all low, every 500 ms, each step read
back:

```
app: header i2c (J19 pins 3/5): 0x20
app: PCF8574 P0-P7 walk: 110 steps, 0 errors (ready after 5 s)
```

The PCF8574 drives its outputs high only weakly (about 0.1 mA): measure
P0-P7 with a meter or logic analyser, or connect LEDs from VCC to the pins
(active low).

The Zephyr DomU's output is also in Xen's console log, so it can be read
from Dom0 without the serial console: `xl dmesg | grep DOM2`.

Tested on hardware (before `xen_demo` was split into part files, with the
same tests): the LED alone and with netfront (LED cycles, DHCP and ping
from the LAN) with `gpio2`; the display bus test with
`lpi2c4` (all three devices found, IDs and register readback correct); the
header I2C with a PCF8574 module (found at 0x20, P0-P7 walk without errors,
DHCP and ping from the LAN), each while Dom0 and the Linux DomU run.
Switching with `epp-passthrough` and rebooting, one demo image: `lpi2c6` (PCF8574 found, Linux keeps `i2c-3`), `none` (GPIO2 only,
Linux keeps all buses), `lpi2c4` (Linux loses `i2c-3`; with
`CONFIG_EPP_LPI2C4` the display bus test passes, without it the bus is left
alone), `lpi2c5 lpi2c6 lpi2c7` together (all three buses started, PCF8574
found on pins 3/5; pins 27/28 and 31/26 scanned without a bus error, but
with no device connected there yet), `lpi2c6 lpspi7` with a W5500 module
(version 0x04, MAC register write and read back correct), and
`epp-passthrough` refusing `lpi2c7` together with `lpspi7`; preset
`header-spi-w5500-eth` (pins set by U-Boot, W5500 with Zephyr's driver:
DHCP and ping from the LAN on both netfront and W5500; with INTn on pin 36
the driver uses the GPIO interrupt, ping 0.8 ms instead of about 7 ms when
polling); `flexcan2` in loopback mode and with J20 wired to J21 (heartbeat
in `candump` on Dom0's `can0`, `cansend` answered in 0.17 ms, both nodes
error-active; with `can0` down the Zephyr DomU keeps running and recovers
when it comes up; warm reboots between combinations with and without
`flexcan2` keep it working, since Linux no longer owns FlexCAN2); `lpuart7` with a wire from pin 24 to pin 21 (loopback
OK at 115200 baud; it failed until LPSPI3 was disabled in Dom0).
