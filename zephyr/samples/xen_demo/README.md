# xen_demo: the Zephyr DomU demo

One Zephyr image for every passthrough selection (see
[docs/xen-passthrough.md](../../../docs/xen-passthrough.md)). The LED D19 on
GPIO2 (always passed through) cycles red, green, blue, white, off; netfront
gets an address by DHCP. Once Dom0 has booted, the application reads the
passthrough list from xenstore (`epp/passthrough`) and starts the **parts**
of the listed devices; every 10 s it prints the network state and every
part's result. Devices are `zephyr,deferred-init` in `app.overlay`: a part
starts its device only when it is passed through, so nothing touches a
device that is not.

The quickest way to build it is with the Yocto image, from a preset:
`./build-demo.sh header-i2c` (see the main
[README](../../../README.md#demo-presets-build-demosh)).

## Files

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

## Parts

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
`app.overlay` is `"okay"`. Each part file defines its parts with
`DEMO_PART()` (an iterable section, run in name order); `CMakeLists.txt`
compiles a file only when its Kconfig option is on. When Dom0 passes a
device through whose part is not compiled in, the application says so
(`app: lpi2c4 is passed through, but its part is not compiled in
(Kconfig)`) and does not touch it.

### Adding a part

1. A node for the device in `app.overlay`, with `zephyr,deferred-init`.
2. A Kconfig option.
3. A file in `src/` with `DEMO_PART(pN_<option>, .option = "<option>",
   .start = ..., .report = ...)`.
4. A `target_sources_ifdef()` line in `CMakeLists.txt`.

## Build by hand

```bash
west build -p -b xenvm/xenvm/gicv3 $HOME/dev/epp-v2/zephyr/samples/xen_demo -- \
    -DEXTRA_ZEPHYR_MODULES=$HOME/dev/epp-v2/zephyr \
    -DEXTRA_DTC_OVERLAY_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.overlay \
    -DEXTRA_CONF_FILE=$HOME/dev/epp-v2/zephyr/xen/xenvm-dom0less.conf
cp build/zephyr/zephyr.bin $HOME/dev/epp-v2/xen/zephyr.bin
```

Extras are added to the two lists, e.g. for the W5500 register test
`...;$HOME/dev/epp-v2/zephyr/samples/xen_demo/extras/w5500.overlay` and
`...;.../extras/w5500.conf`.

## Output

The output is on the Xen serial console (DOM2), and in Dom0 with
`xl dmesg | grep DOM2`. Every 10 s, with `lpi2c5`, `lpi2c6` and `lpi2c7`
selected (a PCF8574 on pins 3/5):

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

With `lpspi7`: the loopback test, or with `extras/w5500.overlay` +
`extras/w5500.conf` the W5500 register test (SCLK 26, MOSI 31, MISO 29,
SCSn 35, RSTn 37), which uses its own compatible (`epp,w5500-test`) so that
no driver binds to the chip:

```
app: header spi: loopback OK (8 bytes)
app: header spi: W5500 version 0x04 OK, link down, MAC write/read OK
```

With `extras/w5500-eth.overlay` + `extras/w5500-eth.conf` (preset
`header-spi-w5500-eth`, needs `EARLY_PINS=lpspi7`) the W5500 is a
`wiznet,w5500` with Zephyr's driver (random MAC address), a second network
interface with its own Ethernet port and DHCP. Without INTn on pin 36,
remove `int-gpios` and the driver polls (log: "polling mode"):

```
app: net: carrier on, mac 00:16:3e:12:ea:d9, ipv4 10.10.193.32
app: w5500: carrier on, mac fa:7b:82:00:db:2b, ipv4 10.10.193.243
```

With `flexcan2` and J20 wired to J21 (Linux's `can0` up, see
[CAN](../../../docs/xen-passthrough.md#can-between-the-zephyr-domu-and-linux-flexcan2)),
or with `extras/can-loopback.conf` (preset `can-loopback`, no wiring; every
10 s the application also sends a request to itself):

```
app: can (J20): error-active (tx err 0, rx err 0), sent 21, received 1, last 0x100 [4] 11 22 33 44
app: can (J20, loopback): error-active (tx err 0, rx err 0), sent 23, received 23, last 0x100 [2] ca fe
```

The application sends with a callback, so it never waits for a frame that
no node acknowledges.

With `lpuart7` and a wire from pin 24 to pin 21 (diagonal, not
neighbours): a line at 115200 baud, byte by byte, every byte expected back:

```
app: header uart (pins 24/21): loopback OK (15 bytes, 115200 baud)
```

## PCF8574 test

A PCF8574 I/O expander module (not on the board) on the header at 0x20
(A0-A2 to GND; a PCF8574A is at 0x38), driven with Zephyr's `nxp,pcf857x`
GPIO driver (`extras/pcf8574.overlay` + `extras/pcf8574.conf`, preset
`header-pcf8574`). Its pins P0-P7 go high one after the other, then all
high, then all low, every 500 ms, each step read back:

```
app: header i2c (J19 pins 3/5): 0x20
app: PCF8574 P0-P7 walk: 110 steps, 0 errors (ready after 5 s)
```

The PCF8574 drives its outputs high only weakly (about 0.1 mA): measure
P0-P7 with a meter or logic analyser, or connect LEDs from VCC to the pins
(active low).

[samples/xen_netfront](../xen_netfront) and
[samples/xen_xenstore](../xen_xenstore) are the minimal examples of the
network frontend and the xenstore client.
