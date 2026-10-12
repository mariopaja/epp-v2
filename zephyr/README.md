# Zephyr for epp-v2

A Zephyr module (`zephyr/module.yml`) for two different uses:

| Use | Zephyr board | Here |
|---|---|---|
| Zephyr on the Cortex-M7 (or bare metal on an A55), owning its hardware | `frdm_imx95` (this module) | `boards/nxp/frdm_imx95/` |
| Zephyr as a Xen DomU next to Linux, on an A55 core | `xenvm/xenvm/gicv3` (upstream) | everything else |

```
zephyr/
  boards/nxp/frdm_imx95/   board for the M7 and A55 targets
  xen/                     overlay + config for xenvm as a dom0less DomU on this board
  drivers/xen/             xenstore client, netfront (Xen network frontend)
  drivers/{gpio,i2c,spi,can,serial}/
                           minimal drivers for passed-through devices (xen_*.c)
  dts/bindings/            their bindings, epp,xen-*
  include/epp/xen/         xenstore client API
  Kconfig, CMakeLists.txt  module options CONFIG_EPP_XEN_*
  samples/                 xen_demo (the demo), xen_netfront, xen_xenstore
```

## Board `frdm_imx95` (M7, A55 bare metal)

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

Enabled on these targets: console UART, eDMA2, LPTMR1 and TSTMR2 (M7). Not
yet enabled there: Ethernet, CAN, I2C, SPI, GPIO expander; pin groups for
CAN2 are prepared in `frdm_imx95-pinctrl.dtsi`.

A Zephyr DomU under Xen is a different board (`xenvm`), with its own
drivers for the devices Xen passes through, see
[Zephyr as a Xen DomU](#zephyr-as-a-xen-domu) and
[Drivers for the Zephyr DomU](#drivers-for-the-zephyr-domu).

**Status:** builds without warnings (Zephyr main of 2026-10-04, Zephyr SDK
1.0.1) for `samples/hello_world`, `samples/synchronization` and
`samples/subsys/shell/shell_module` on both targets. Tested on hardware:
`samples/synchronization` on the M7, started from the boot container
(`EPP_M7_IMAGE`). The A55 target is not yet tested on hardware. No LEDs are
defined, so `samples/basic/blinky` does not build.

### Build

Inside a Zephyr workspace (`west init` / `west update`, Zephyr SDK installed):

```bash
west build -p -b frdm_imx95/mimx9596/m7 samples/hello_world \
    -- -DBOARD_ROOT=$HOME/dev/epp-v2/zephyr
```

Or add this directory as a Zephyr module (`zephyr/module.yml` sets the board
root) with `-DEXTRA_ZEPHYR_MODULES=$HOME/dev/epp-v2/zephyr`.

### Run on the M7

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

### What the DomU sees

Xen gives the domain a small virtual machine, not the SoC:

| | In the Zephyr DomU |
|---|---|
| CPU | one vCPU (`EPP_XEN_ZEPHYR_VCPUS`), pinned to a physical A55 core (below) |
| RAM | 16 MB at `0x40000000` (guest address; `EPP_XEN_ZEPHYR_MEM`) |
| Interrupt controller | virtual GICv3 (Xen's vGIC), architected timer |
| Console | PL011 emulated by Xen at `0x22000000` (`vpl011`) |
| Xen interfaces | hypercalls, event channels, grant tables, xenstore (`xen,enhanced`) |
| SoC devices | only those passed through, at their physical addresses, with their interrupts under the same GIC SPI numbers |
| Not there | clock controller, pin controller, SCMI, power domains, DMA, IOMMU |

```
 Zephyr DomU (core 5)                         Dom0 (Linux)
+----------------------------------------+   +------------------------------+
| application (samples/xen_demo)         |   | epp-v2-dom0less-setup,       |
| Zephyr: GPIO, I2C, SPI, CAN, UART, net |   | xenstored, netback, xenbr0,  |
| this module: xen_rgpio, xen_lpi2c, ... |   | pinctrl hog (header pins),   |
|              xenstore client, netfront |   | SCMI to the System Manager   |
+-------------------+--------------------+   +---------------+--------------+
   registers, IRQs  | xenstore, rings, event channels          |
+-------------------v-----------------------------------------v--------------+
| Xen: stage-2 MMU (maps the passed-through registers 1:1), vGIC, scheduler |
+-----------------------------------------------------------------------------+
| SoC: GPIO2, LPI2Cx, LPSPI7, FlexCAN2, LPUART7 | clocks, pins: System Manager |
+-----------------------------------------------------------------------------+
```

### Start-up

1. U-Boot runs `boot.scr`, which starts Xen; Xen creates Dom0, the Linux
   DomU and the Zephyr DomU at once (dom0less), Zephyr on its own core.
2. Zephyr boots and starts GPIO2 (always passed
   through). The LED shows once Dom0's Linux has set its pins (about 5 s).
3. The xenstore client waits until Dom0 (`init-dom0less`) has set up the
   domain's xenstore.
4. `epp-v2-dom0less-setup` in Dom0 attaches the vif (netfront connects,
   DHCP) and writes the passthrough list (`epp/passthrough`).
5. The application starts the passed-through devices from that list
   (`device_init()` of `zephyr,deferred-init` nodes).

The Zephyr DomU gets its own physical core by default
(`EPP_XEN_ZEPHYR_CPU="5"`): Xen puts it in a cpupool with the `null`
scheduler, so its vCPU is never moved or shared with Dom0 or the Linux DomU.

To replace only the Zephyr DomU on a running board, copy the new
`zephyr.bin` to the boot partition as `zephyr-domu.bin` (from Dom0:
`mount /dev/mmcblk1p1 /mnt`) and restart the board; `./build-demo.sh
<preset> --update` does this over SSH.

The DomU's output is also in Xen's console log, readable in Dom0 without the
serial console: `xl dmesg | grep DOM2` (Xen's buffer is small: the first
lines after boot may be gone).

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

Xen can give the Zephyr DomU real devices of the SoC: GPIO2 with the RGB
LED always, plus any combination of I2C (`lpi2c4`-`lpi2c7`), SPI
(`lpspi7`), CAN (`flexcan2`) and UART (`lpuart7`), selected on the board
with `epp-passthrough` in Dom0. The options, the 40-pin header map, how it
works and what is tested: [docs/xen-passthrough.md](../docs/xen-passthrough.md).

## Drivers for the Zephyr DomU

### Why own drivers

Zephyr has drivers for all these controllers (`gpio_mcux_rgpio.c`,
`i2c_mcux_lpi2c.c`, `spi_nxp_lpspi`, `can_mcux_flexcan.c`,
`uart_mcux_lpuart.c`), used on NXP's MCUs and the Cortex-M cores of i.MX SoCs. They are written
for a Zephyr that owns the SoC, and a DomU does not:

- **Clocks.** Each upstream driver takes its clock from a clock controller
  (`clocks` in the device tree; `clock_control_on()`, `_get_rate()`,
  `_configure()` at init; FlexCAN and LPUART `depends on CLOCK_CONTROL`).
  On the i.MX 95 the clocks belong to the System Manager, reached over SCMI
  (shared memory and a messaging unit). The A55 side's SCMI channel is
  Dom0's; the DomU has none. Giving it one would need its own agent in the
  System Manager configuration and passing a messaging unit and its shared
  memory through to the DomU.
- **Pins.** They apply their pin state at init (`pinctrl_apply_state()`,
  `select PINCTRL`), and pin control on the i.MX 95 is SCMI too. Here Dom0
  (or U-Boot, `EPP_XEN_ZEPHYR_EARLY_PINS`) sets the pins.
- **NXP HAL.** They wrap the MCUXpresso SDK drivers of `hal_nxp`
  (`fsl_lpi2c.h`, `fsl_flexcan.h`, ...), which need the SoC's device
  headers and `CONFIG_HAS_MCUX`. `xenvm` is a generic arm64 virtual
  machine and has neither.

One way around it would be a `xenvm` variant with the i.MX 95 headers, a
fixed-rate clock controller and a pin controller that does nothing. That is
a custom SoC definition kept in step with Zephyr; for the devices here, a
driver that only knows the registers is a few hundred lines and has no
other dependency.

### What they do instead

- **Registers only**, mapped with `DEVICE_MMIO` from the node's `reg`
  (Xen maps the physical address 1:1 into the DomU).
- **Clock rate from the device tree**: `functional-clock-frequency`
  (24 MHz, the System Manager's default for these clocks), since they
  cannot ask for it.
- **No pin control**: the node has no `pinctrl-0`; the pads are already
  set when the driver starts.
- **Interrupts** from the node's `interrupts`, the same GIC SPI number as
  on the host (only GPIO2 and FlexCAN2 use them; the others poll).
- **Standard Zephyr APIs** (GPIO, I2C, SPI, CAN, UART polling), so
  Zephyr's own device drivers and subsystems work on top: the W5500
  Ethernet driver on `xen_lpspi` and `xen_rgpio`, the PCF8574 GPIO driver
  on `xen_lpi2c`, the CAN API on `xen_flexcan`.
- **No DMA**: the SMMU is off, so a passed-through device must not do DMA.

A node in the application's overlay, for example LPI2C6:

```dts
lpi2c6: i2c@426c0000 {
	compatible = "epp,xen-lpi2c";
	reg = <0x00 0x426c0000 0x00 0x10000>;   /* physical address, mapped 1:1 by Xen */
	#address-cells = <1>;
	#size-cells = <0>;
	clock-frequency = <I2C_BITRATE_STANDARD>;
	zephyr,deferred-init;                   /* started only when passed through */
	status = "okay";
};
```

### The drivers

| Kconfig | Driver |
|---|---|
| `CONFIG_EPP_XEN_RGPIO` | RGPIO, `epp,xen-rgpio`: inputs and outputs; pin interrupts (rising, falling, both edges, low and high level, through the interrupt control registers ICR and the flags in ISFR) when the node has the interrupt Xen routes; no pull or open-drain flags (pad settings belong to the pin mux). The secure-only registers PCNS/PCNP must not be accessed from the DomU; reading them aborts |
| `CONFIG_EPP_XEN_LPI2C` | LPI2C controller, `epp,xen-lpi2c`: 7-bit addresses, standard (100 kHz) and fast mode (400 kHz), polled. The SCL timing assumes the 24 MHz functional clock (`functional-clock-frequency`) |
| `CONFIG_EPP_XEN_LPUART` | LPUART, `epp,xen-lpuart`: polled (`poll_in`, `poll_out`, `err_check`), 8N1, baud rate from `current-speed` (oversampling and divider with the smallest error from the 24 MHz clock), optional `internal-loopback` |
| `CONFIG_EPP_XEN_FLEXCAN` | FlexCAN, `epp,xen-flexcan`: classic CAN (no CAN FD), standard and extended IDs, remote frames, 8 receive filters (one mailbox each, individual masks), one transmit mailbox, normal, loopback and listen-only mode, state changes; interrupt driven. Start-up and i.MX95 quirks as in Linux's flexcan driver (ECC RAM init, MECR, reserved mailbox 0). Bit timing from the 24 MHz CAN clock (`functional-clock-frequency`) |
| `CONFIG_EPP_XEN_LPSPI` | LPSPI controller, `epp,xen-lpspi`: controller mode, 8-bit words, full duplex, SPI modes 0-3, MSB or LSB first, polled, chip select by GPIO (`cs-gpios`). SCK = 24 MHz / (2^prescale x (div + 2)), up to 12 MHz |

## The demo application

[samples/xen_demo](samples/xen_demo/README.md) runs the LED, netfront and a
test for every passthrough option, in one image for every selection.
[samples/xen_netfront](samples/xen_netfront) and
[samples/xen_xenstore](samples/xen_xenstore) are the minimal examples of the
network frontend and the xenstore client.
