# Xen passthrough to the Zephyr DomU

With `./docker-run.sh --xen` (dom0less) and a Zephyr DomU
(`EPP_XEN_ZEPHYR_IMAGE`), Xen gives devices to the Zephyr DomU, mapped at
their physical addresses; Dom0 then no longer sees them (`status =
"disabled"` in its device tree). GPIO2 with the RGB LED is always passed
through, plus any combination of the options below, selected on the board
without a rebuild.

The Zephyr side: the drivers in [zephyr/drivers](../zephyr/drivers) (see
[zephyr/README.md](../zephyr/README.md#drivers-for-the-zephyr-domu)) and the
demo application [zephyr/samples/xen_demo](../zephyr/samples/xen_demo/README.md),
which has a test for every option. `./build-demo.sh` builds both with the
image (see the main [README](../README.md#demo-presets-build-demosh)).

## Options

| Name | Device | Address | On the FRDM-IMX95 | Zephyr compatible |
|---|---|---|---|---|
| `gpio2` (always) | GPIO2 | `0x43810000`, interrupt GIC SPI 49 | RGB LED D19 (green GPIO_IO04, blue GPIO_IO12, red GPIO_IO13, active high), GPIO_IOxx pins of the 40-pin header | `epp,xen-rgpio` |
| `lpi2c4` | LPI2C4 (Linux `i2c-3`) | `0x42540000` | display bus: IT6263 LVDS-to-HDMI bridge (0x4c, 0x33), PCA9632 backlight LED driver (0x62); Linux loses HDMI | `epp,xen-lpi2c` |
| `lpi2c5` | LPI2C5 (disabled in Linux) | `0x426b0000` | I2C of the 40-pin header: pin 27 SDA, pin 28 SCL | `epp,xen-lpi2c` |
| `lpi2c6` | LPI2C6 (disabled in Linux) | `0x426c0000` | I2C of the 40-pin header: pin 3 SDA, pin 5 SCL | `epp,xen-lpi2c` |
| `lpi2c7` | LPI2C7 (disabled in Linux) | `0x426d0000` | I2C of the 40-pin header: pin 31 SDA, pin 26 SCL | `epp,xen-lpi2c` |
| `lpspi7` | LPSPI7 (disabled in Linux) | `0x42710000` | SPI of the 40-pin header: SCK 26, MOSI 31, MISO 29, chip select 35, reset 37, interrupt 36 | `epp,xen-lpspi` |
| `flexcan2` | FlexCAN2 (disabled in Linux, also without passthrough) | `0x425b0000`, interrupt GIC SPI 38 | CAN2, transceiver TJA1051, connector J20; Linux keeps CAN5 (J21) | `epp,xen-flexcan` |
| `lpuart7` | LPUART7 (disabled in Linux) | `0x42690000` | UART of the 40-pin header: pin 24 TX, pin 21 RX; Dom0 then switches U16 to the header and leaves LPSPI3 (M.2 slot SPI) off | `epp,xen-lpuart` |

Any combination works, except `lpi2c7` with `lpspi7`: both use pins 31/26
(`EPP_XEN_ZEPHYR_PT_CONFLICTS` in layer.conf). `epp-passthrough` refuses to
select both.

## 40-pin header (J19)

The header pins the options use (schematic page 30). **3.3 V only**, and
the board has no I2C pull-ups on these pins (see
[I2C](#i2c-on-the-40-pin-header-lpi2c5-lpi2c6-lpi2c7)).

| Pin | Pad | Used by |
|---|---|---|
| 1, 17 | | 3.3 V |
| 6, 9, 14, 20, 25, 30, 34, 39 | | GND |
| 3 | GPIO_IO02 | `lpi2c6` SDA |
| 5 | GPIO_IO03 | `lpi2c6` SCL |
| 7 | GPIO_IO04 | `gpio2`: green LED |
| 21 | GPIO_IO09 | `lpuart7` RX |
| 24 | GPIO_IO08 | `lpuart7` TX |
| 26 | GPIO_IO07 | `lpi2c7` SCL **or** `lpspi7` SCK |
| 27 | GPIO_IO00 | `lpi2c5` SDA |
| 28 | GPIO_IO01 | `lpi2c5` SCL |
| 29 | GPIO_IO05 | `lpspi7` MISO |
| 31 | GPIO_IO06 | `lpi2c7` SDA **or** `lpspi7` MOSI |
| 32 | GPIO_IO12 | `gpio2`: blue LED |
| 33 | GPIO_IO13 | `gpio2`: red LED |
| 35 | GPIO_IO19 | `lpspi7` chip select (GPIO2 19) |
| 36 | GPIO_IO16 | `lpspi7` device interrupt (GPIO2 16), optional |
| 37 | GPIO_IO26 | `lpspi7` device reset (GPIO2 26), optional |

CAN: J20 (CAN2, `flexcan2`) and J21 (CAN5, Linux), PHB 2.0 mm, pin 1 CANH,
2 CANL, 3 GND.

## Selecting the passthrough

In Dom0 (SSH or serial console), `epp-passthrough` shows the options; a
number ticks or unticks one, `s` saves the selection for the next boot:

```
root@epp-v2-dom0:~# epp-passthrough
Xen passthrough to the Zephyr DomU

         always  gpio2    GPIO2: RGB LED D19, GPIO_IOxx pins of the 40-pin header

  1  [ ]          lpi2c4   LPI2C4: display I2C bus (IT6263 HDMI bridge, PCA9632), Linux loses HDMI
  2  [ ]          lpi2c5   LPI2C5: I2C of the 40-pin header, pin 27 SDA, pin 28 SCL
  3  [x]          lpi2c6   LPI2C6: I2C of the 40-pin header, pin 3 SDA, pin 5 SCL
  4  [ ]          lpi2c7   LPI2C7: I2C of the 40-pin header, pin 31 SDA, pin 26 SCL
  5  [ ]          lpspi7   LPSPI7: SPI of the 40-pin header, SCK 26, MOSI 31, MISO 29, CS 35 (GPIO), RST 37
  6  [x]          flexcan2 FlexCAN2: CAN2, connector J20 (Linux keeps CAN5, J21)
  7  [ ]          lpuart7  LPUART7: UART of the 40-pin header, pin 24 TX, pin 21 RX (M.2 SPI off)

  running now: gpio2,lpi2c6,flexcan2
  next boot:   gpio2 lpi2c6 flexcan2

Toggle 1-7, s to save, q to quit:
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

## How it works

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
   sets them once at boot instead (SCMI pinctrl hog of Dom0):

   | Option | Pads |
   |---|---|
   | `gpio2` | GPIO_IO04, GPIO_IO12, GPIO_IO13 (the LED) |
   | `lpi2c4` | GPIO_IO30/31 |
   | `lpi2c5` | GPIO_IO00/01 |
   | `lpi2c6` | GPIO_IO02/03 |
   | `lpi2c7` | GPIO_IO06/07 |
   | `lpspi7` | GPIO_IO05-07, and GPIO_IO16/19/26 as GPIO |
   | `flexcan2` | GPIO_IO25/27 |
   | `lpuart7` | GPIO_IO08/09 |

   To use other GPIO2 pins of the 40-pin header, add their
   `IMX95_PAD_GPIO_IOxx__GPIO2_IO_BITxx` entries to
   `epp-zephyr-gpio2-pins.dtsi` and rebuild the image. The same overlay
   carries the other changes Dom0's device tree needs (fragments
   `*-dom0.dtsi`, below).
3. **Zephyr:** `boot.scr` passes the list to Dom0 (`epp.passthrough=gpio2,lpi2c6`
   on its command line); `epp-v2-dom0less-setup` writes it to the Zephyr
   DomU's xenstore (`epp/passthrough`, readable only by that domain). Zephyr
   does not discover devices from Xen: the application has nodes for all of
   them (`zephyr,deferred-init`) and starts a device (`device_init()`) only
   when it is in that list. Touching a device that is not passed through
   aborts.

The devices are defined once, in [layer.conf](../meta-epp-v2/conf/layer.conf)
(`EPP_XEN_ZEPHYR_PT_FIXED`, `EPP_XEN_ZEPHYR_PT_OPTIONS`, and per device
`EPP_XEN_ZEPHYR_PT_NODE[name]` and `EPP_XEN_ZEPHYR_PT_DESC[name]`), plus
fragments in `meta-epp-v2/recipes-bsp/epp-v2-overlays/files/`:

| Fragment | Content |
|---|---|
| `epp-zephyr-<name>-pt.dtsi` | node of the partial device tree: `xen,path`, `xen,reg`, `xen,force-assign-without-iommu`, optionally `interrupts` |
| `epp-zephyr-<name>-pins.dtsi` | pin group `pinctrl_epp_<name>` for the hog |
| `epp-zephyr-<name>-dom0.dtsi` | optional: other changes to Dom0's device tree while the device is passed through (top level of the pin mux overlay); `flexcan2` keeps the CAN transceivers on, `lpuart7` switches U16 and disables LPSPI3 |
| `epp-zephyr-dom0.dtsi` | changes to Dom0's device tree in every combination: FlexCAN2 off (reserved for the Zephyr DomU) |

From these the build generates the files per combination (`epp-v2-overlays`,
combinations: `EPP_XEN_ZEPHYR_PT_COMBOS`), the combination table in
`boot.scr` (`epp-v2-bootscript`) and the list of `epp-passthrough`
(`epp-v2-xen-dom0`). Options that share pins or controllers go into
`EPP_XEN_ZEPHYR_PT_CONFLICTS` (`a:b` pairs); the build leaves out their
combinations and stops if the default selects both. Each option doubles the
number of combinations.

**Adding a device:**

1. Write its fragments (`-pt`, `-pins`, and `-dom0` if Dom0's device tree
   needs more than the `disabled` node).
2. Add it to layer.conf (`EPP_XEN_ZEPHYR_PT_OPTIONS`, `_NODE`, `_DESC`,
   `_CONFLICTS` if it shares pins) and to the name checks in
   `scripts/yocto-build.sh` and `build-demo.sh`.
3. Give Zephyr a driver and a node for it, and the demo application a part
   (see [xen_demo](../zephyr/samples/xen_demo/README.md#adding-a-part)).

Only devices without DMA work: the SMMU is off for Xen, so a passed-through
device that does DMA could write to any memory.

**Interrupts:** a device's interrupt goes to the DomU when its fragment has
`interrupts` with `interrupt-parent = <0xfde8>` (65000, Xen's phandle for
the guest GIC); Xen routes the physical SPI to the DomU under the same
number, and Dom0 no longer gets it. `gpio2` (GIC SPI 49, GPIO pin
interrupts) and `flexcan2` (GIC SPI 38) do this; the Zephyr node needs the
same interrupt (`interrupts = <GIC_SPI 49 IRQ_TYPE_LEVEL
IRQ_DEFAULT_PRIORITY>`). The I2C, SPI and UART controllers are polled.

**Clocks and power** stay with the System Manager. The clocks of the
passed-through controllers (24 MHz) are on from boot, also for controllers
Linux never uses, and Dom0 boots with `clk_ignore_unused`. A device that
Linux drives can still lose its clock through Linux's runtime power
management, which is why FlexCAN2 is disabled in Dom0 in every combination
(see [CAN](#can-between-the-zephyr-domu-and-linux-flexcan2)). The System
Manager configuration (`mx95evk.cfg`) gives these controllers and their
pads to the A55 side (`LM2 "AP"`), not to the M7. The board's "PWM/GPIO
select" (LED instead of PWM) is a gpio-hog in the FRDM device tree, set by
Dom0.

**Start-up timing:** Xen starts the Zephyr DomU together with Dom0, but
Dom0's pinctrl hog switches the pins only when its Linux boots, about 5 s
later. The demo application starts a device only after Dom0 has written the
passthrough list, so this is already over. A Zephyr driver that accesses
its device at boot needs `EPP_XEN_ZEPHYR_EARLY_PINS` (see
[SPI](#spi-on-the-40-pin-header-lpspi7)).

## I2C on the 40-pin header (`lpi2c5`, `lpi2c6`, `lpi2c7`)

Three I2C buses of the header J19 are usable, all with pins that go
directly to the SoC and controllers nothing else on the board uses:

| Option | SDA | SCL | Note |
|---|---|---|---|
| `lpi2c6` | pin 3 (GPIO_IO02) | pin 5 (GPIO_IO03) | the usual I2C pins of a Raspberry Pi header |
| `lpi2c5` | pin 27 (GPIO_IO00) | pin 28 (GPIO_IO01) | ID_SD / ID_SC on a Raspberry Pi: a HAT's ID EEPROM appears here |
| `lpi2c7` | pin 31 (GPIO_IO06) | pin 26 (GPIO_IO07) | not with `lpspi7` |

Not usable: the same pins as LPI2C4 (pins 3/5) or LPI2C3 (pins 27/28),
because those controllers already serve the display and USB-C buses on
other pins and each I2C signal has one input select; LPI2C6 on pins 7/29
(pin 7 is the green LED); LPI2C7/8 on pins 21/24 and 19/23, and LPI2C5 on
pins 15/16, which go through the board's select switches (schematic page
18); LPI2C8 on pins 32/33 (blue and red LED).

- **No pull-ups on the board** (R660-R663 are not fitted for pins 3/5 and
  27/28, none for pins 31/26). The connected module needs its own (most
  breakout boards have them), or add about 2.2-4.7 kOhm from SDA and SCL to
  3.3 V. The pads have the SoC's weak internal pull-ups enabled, enough for
  an idle bus but not for reliable transfers.
- **3.3 V only.** Power modules with pull-ups from pin 1, never from 5 V: the
  pull-ups would put 5 V on the SoC pins.
- **Start-up timing:** until Dom0 has switched the pins (about 5 s after the
  Zephyr DomU started) every transfer fails (`-EIO`); code that does not
  wait for the passthrough list must retry its first accesses.

## SPI on the 40-pin header (`lpspi7`)

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

The demo application tests the bus in a loopback (jumper from MOSI pin 31
to MISO pin 29), with a WIZnet W5500 module in a register test, or uses the
W5500 as a second network interface with Zephyr's driver (see
[xen_demo](../zephyr/samples/xen_demo/README.md)). INTn of the W5500 on
pin 36 (`int-gpios = <&gpio2 16 GPIO_ACTIVE_LOW>`) lets the driver wait for
the GPIO interrupt instead of polling the chip.

Zephyr's network stack and the W5500 driver access the chip at boot, so two
things change compared with the other devices:

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

## CAN between the Zephyr DomU and Linux (`flexcan2`)

The board has three CAN transceivers (TJA1051, schematic page 31) with
termination: CAN2 on connector J20 and CAN5 on J21 (PHB 2.0 mm, pin 1
CANH, 2 CANL, 3 GND; CAN1 goes to the connector of page 19). With
`flexcan2` the Zephyr DomU gets FlexCAN2 and its interrupt; Linux in Dom0
keeps FlexCAN5 (`can0`). Connect J20 to J21 (1-1, 2-2, 3-3) for a bus
between the two:

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

- **J20 reserved for the Zephyr DomU:** FlexCAN2 is disabled in Dom0 in
  every combination (`epp-zephyr-dom0.dtsi`), also when `flexcan2` is not
  passed through. When Linux owned it, its runtime power management turned
  the CAN2 clock off through the System Manager; that survives a warm
  reboot, and FlexCAN2 then stayed in low-power mode in the next boot that
  passed it through (driver: "no response (MCR ...)") until a power cycle.
  After updating from an image without this fragment (before v0.8.8),
  power cycle once.
- **Bit rate:** 500 kbit/s from the CAN2 clock root, 24 MHz (the System
  Manager's default): prescaler 3, 16 time quanta, sample point 87.5 %.
- **Transceiver standby:** the standby line (`CAN_STBY`, GPIO expander
  line 7, regulator `can-stby` in Linux) is shared by the three
  transceivers, and Linux switches it on only while one of its CAN
  interfaces is up. `epp-zephyr-flexcan2-dom0.dtsi` makes it
  `regulator-always-on` while `flexcan2` is passed through. Without it the
  Zephyr DomU's frames do not reach the bus (bit errors, bus-off) while
  Linux's `can0` is down.
- **CAN2/CAN5 switch:** the pins reach the transceivers through U19
  (schematic page 18) while "CAN2&5/GPIO select" is low, a gpio-hog of the
  FRDM device tree that Dom0 sets independently of FlexCAN2.
- **No other node:** without a node that acknowledges (Linux's `can0`
  down, nothing wired) the Zephyr DomU's frames are retransmitted and it
  goes error-passive; it recovers by itself once another node is on the bus
  (also from bus-off).

Without wiring, the demo application's loopback mode (preset
`can-loopback`) receives its own frames inside FlexCAN2.

## UART on the 40-pin header (`lpuart7`)

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
LPUART5). The binding's `internal-loopback` property loops TX to RX inside
the LPUART (CTRL[LOOPS]), to test the driver without the pin path.

## Tested on hardware

With Dom0 and the Linux DomU running. Application: `xen_demo` is the
current demo; *old* is its predecessor `xen_led_i2c_netfront` (up to
v0.8.7), with the same test code.

| Selection | Setup | Result | Application |
|---|---|---|---|
| `gpio2` (always) | | LED D19 cycles; netfront: DHCP and ping from the LAN | `xen_demo` |
| `none` | | GPIO2 only, Linux keeps all buses | old |
| `lpi2c4` | display bus | IT6263 and PCA9632 found, chip ID and register readback correct; Linux loses `i2c-3`. Without `CONFIG_EPP_LPI2C4` the bus is left alone | old |
| `lpi2c6` | PCF8574 module on pins 3/5 | found at 0x20, P0-P7 walk without errors; Linux keeps `i2c-3` | old |
| `lpi2c5 lpi2c6 lpi2c7` | PCF8574 on pins 3/5 | all three buses started, PCF8574 found; pins 27/28 and 31/26 scanned without a bus error (nothing connected there) | old |
| `lpspi7` | jumper pin 31 to 29 | loopback OK | old |
| `lpi2c6 lpspi7` | W5500 module | version 0x04, MAC register write and read back correct | old |
| `lpi2c6 lpspi7`, `EARLY_PINS=lpspi7` | W5500 module, INTn on pin 36 | DHCP and ping from the LAN on netfront and W5500; ping 0.8 ms with the interrupt, about 7 ms polling | old |
| `flexcan2` | loopback mode | every frame received | old |
| `flexcan2` | J20 wired to J21 | heartbeat in `candump` on Dom0's `can0`, `cansend` answered in 0.17 ms, both error-active; with `can0` down the Zephyr DomU keeps running and recovers when it comes up | old |
| `flexcan2` | J20 wired to J21, warm reboots from `lpuart7` and from `none` | CAN starts and works in both directions | `xen_demo` |
| `lpuart7` | wire pin 24 to pin 21 | loopback OK at 115200 baud (failed until LPSPI3 was disabled in Dom0) | `xen_demo` |
| switching | `epp-passthrough`, one image | every selection above; refuses `lpi2c7` with `lpspi7` | old |
