/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr Xen DomU with the shared Ethernet (netfront, DHCP) and the devices
 * Xen passes through: GPIO2 cycles the FRDM-IMX95 RGB LED D19 (red, green,
 * blue, white, off) every second. Which I2C bus is passed through as well
 * is selected in Dom0 (epp-passthrough) and listed by Dom0 in xenstore
 * (epp/passthrough, e.g. "gpio2,lpi2c5,lpi2c6"); only those buses are
 * started. The parts for the buses are compiled in with Kconfig (see
 * Kconfig):
 *
 *   CONFIG_EPP_LPI2C4   lpi2c4, display bus: scan, IT6263 chip ID, PCA9632
 *                       register write and read back
 *   CONFIG_EPP_LPI2C5   lpi2c5, 40-pin header pins 27/28: scan
 *   CONFIG_EPP_LPI2C6   lpi2c6, 40-pin header pins 3/5: scan
 *   CONFIG_EPP_LPI2C7   lpi2c7, 40-pin header pins 31/26: scan
 *   CONFIG_EPP_LPSPI7   lpspi7, 40-pin header SPI: loopback test (jumper
 *                       MOSI pin 31 to MISO pin 29)
 *   CONFIG_EPP_W5500    W5500 module on lpspi7: version, PHY state, MAC
 *                       register write and read back (instead of the
 *                       loopback test)
 *   CONFIG_EPP_PCF8574  PCF8574 module on pins 3/5: its pins P0-P7 go high
 *                       one after the other, then all high, then all low,
 *                       every step read back
 *
 * Every 10 s the network state and the I2C results are printed. Answers
 * pings from the network.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <epp/xen/xenstore.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue), gpios),
};

static const struct {
	const char *name;
	uint8_t on; /* bit 0 red, 1 green, 2 blue */
} steps[] = {
	{"red", 0x1}, {"green", 0x2}, {"blue", 0x4}, {"white", 0x7}, {"off", 0x0},
};

/* Passthrough list from xenstore, set once by the passthrough thread */
static char passthrough[64] = "(waiting for Dom0)";

/* Is @p name in the comma-separated passthrough list? */
static bool passed_through(const char *name)
{
	size_t len = strlen(name);

	for (const char *p = passthrough; p != NULL; p = strchr(p, ',')) {
		if (*p == ',') {
			p++;
		}
		if ((strncmp(p, name, len) == 0) && ((p[len] == ',') || (p[len] == '\0'))) {
			return true;
		}
	}
	return false;
}

/* Start a device marked zephyr,deferred-init in app.overlay */
static bool start_device(const struct device *dev, const char *name)
{
	int ret = device_init(dev);

	if ((ret != 0) && (ret != -EALREADY)) {
		printk("app: %s: init failed (%d)\n", name, ret);
		return false;
	}
	return true;
}

/* Print the addresses that answer on @p bus */
static void scan(const struct device *bus, const char *label)
{
	int found = 0;

	printk("app: %s:", label);
	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		/* Address only, no data: the device answers with ACK or not */
		int ret = i2c_write(bus, NULL, 0, addr);

		if (ret == 0) {
			printk(" 0x%02x", addr);
			found++;
		} else if (ret != -ENXIO) {
			/* Not a NACK: bus stuck (no pull-ups?), stop the scan */
			printk(" bus error %d at 0x%02x, check wiring and pull-ups\n", ret, addr);
			return;
		}
	}
	printk("%s\n", (found == 0) ? " no devices" : "");
}

/* ---- lpi2c4: display bus ---------------------------------------------- */

#ifdef CONFIG_EPP_LPI2C4
#define IT6263_HDMI_ADDR 0x4c
#define PCA9632_ADDR     0x62
#define PCA9632_PWM0     0x02

static const struct device *const display_i2c = DEVICE_DT_GET(DT_NODELABEL(lpi2c4));
static atomic_t display_on;

static void display_start(void)
{
	if (start_device(display_i2c, "lpi2c4")) {
		atomic_set(&display_on, 1);
	}
}

/* Scan, IT6263 chip ID and a PCA9632 register write and read back */
static void display_report(uint32_t round)
{
	static const uint8_t hdmi_id[4] = {0x01, 0xca, 0x13, 0x76};
	uint8_t pattern = (round & 1U) ? 0xa5 : 0x5a;
	uint8_t reg = 0x00;
	uint8_t id[4];
	uint8_t old, back;

	if (!atomic_get(&display_on)) {
		return;
	}
	scan(display_i2c, "display i2c");

	bool id_ok = (i2c_write_read(display_i2c, IT6263_HDMI_ADDR, &reg, 1, id, sizeof(id)) == 0) &&
		     (memcmp(id, hdmi_id, sizeof(id)) == 0);
	bool rw_ok = (i2c_reg_read_byte(display_i2c, PCA9632_ADDR, PCA9632_PWM0, &old) == 0) &&
		     (i2c_reg_write_byte(display_i2c, PCA9632_ADDR, PCA9632_PWM0, pattern) == 0) &&
		     (i2c_reg_read_byte(display_i2c, PCA9632_ADDR, PCA9632_PWM0, &back) == 0) &&
		     (back == pattern) &&
		     (i2c_reg_write_byte(display_i2c, PCA9632_ADDR, PCA9632_PWM0, old) == 0);

	printk("app: display i2c: IT6263 ID %s, PCA9632 write/read 0x%02x %s\n",
	       id_ok ? "OK" : "FAILED", pattern, rw_ok ? "OK" : "FAILED");
}
#else
static void display_start(void)
{
	printk("app: lpi2c4 is passed through, but CONFIG_EPP_LPI2C4 is off\n");
}

static void display_report(uint32_t round)
{
	ARG_UNUSED(round);
}
#endif /* CONFIG_EPP_LPI2C4 */

/* ---- 40-pin header: lpi2c5, lpi2c6, lpi2c7 ----------------------------- */

#ifdef CONFIG_EPP_PCF8574
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_PATH(zephyr_user), pcf8574_gpios),
	     "CONFIG_EPP_PCF8574 needs pcf8574.overlay (pcf8574 and zephyr,user nodes)");

/* PCF8574 P0-P7, from pcf8574-gpios of the zephyr,user node */
#define PCF_GPIO(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec pcf_pins[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pcf8574_gpios, PCF_GPIO)};

/* PCF8574 walk, counted by the passthrough thread, printed by main */
static atomic_t pcf_steps;
static atomic_t pcf_errors;
static atomic_t pcf_setup_s; /* seconds until the PCF8574 answered */

/*
 * One pin high after the other (the others low), then all high, then all
 * low, 500 ms per step. Every step is read back from the pins: a high
 * output is the PCF8574's weak pull-up, so a pin pulled low from outside
 * reads back low and counts as an error. Does not return.
 */
static void pcf8574_walk(void)
{
	if (!start_device(pcf_pins[0].port, "PCF8574")) {
		return;
	}

	/* The driver only writes pins configured as outputs */
	for (uint32_t s = 1U;; s++) {
		bool ok = true;

		for (size_t pin = 0U; ok && (pin < ARRAY_SIZE(pcf_pins)); pin++) {
			ok = (gpio_pin_configure_dt(&pcf_pins[pin], GPIO_OUTPUT_INACTIVE) == 0);
		}
		if (ok) {
			atomic_set(&pcf_setup_s, s);
			break;
		}
		k_msleep(1000);
	}

	for (uint32_t step = 0U;; step++) {
		uint32_t s = step % 10U;
		uint32_t out = (s < 8U) ? BIT(s) : ((s == 8U) ? 0xffU : 0x00U);
		uint32_t in = 0U;
		bool ok = true;

		for (size_t pin = 0U; pin < ARRAY_SIZE(pcf_pins); pin++) {
			ok = ok && (gpio_pin_set_dt(&pcf_pins[pin], (out >> pin) & 1U) == 0);
		}
		for (size_t pin = 0U; ok && (pin < ARRAY_SIZE(pcf_pins)); pin++) {
			int val = gpio_pin_get_dt(&pcf_pins[pin]);

			ok = (val >= 0);
			in |= (uint32_t)(val > 0) << pin;
		}
		if (!ok || (in != out)) {
			atomic_inc(&pcf_errors);
			printk("app: PCF8574 step %u: wrote 0x%02x read 0x%02x\n", s,
			       (unsigned int)out, (unsigned int)in);
		}
		atomic_inc(&pcf_steps);
		k_msleep(500);
	}
}
#endif /* CONFIG_EPP_PCF8574 */

struct header_bus {
	const char *name;  /* passthrough option */
	const char *label;
	const struct device *dev;
	atomic_t on;
};

#define HEADER_BUS(n, pins)                                                                        \
	{.name = "lpi2c" #n,                                                                       \
	 .label = "header i2c (J19 pins " pins ")",                                                \
	 .dev = DEVICE_DT_GET(DT_NODELABEL(lpi2c##n))},

/* The header buses compiled in, each with its Kconfig option */
static struct header_bus header_buses[] = {
#ifdef CONFIG_EPP_LPI2C5
	HEADER_BUS(5, "27/28")
#endif
#ifdef CONFIG_EPP_LPI2C6
	HEADER_BUS(6, "3/5")
#endif
#ifdef CONFIG_EPP_LPI2C7
	HEADER_BUS(7, "31/26")
#endif
};

/* Start the passed-through header buses; runs in the passthrough thread */
static void header_start(void)
{
	static const char *const all[] = {"lpi2c5", "lpi2c6", "lpi2c7"};

	for (size_t i = 0U; i < ARRAY_SIZE(all); i++) {
		bool compiled = false;

		for (size_t b = 0U; b < ARRAY_SIZE(header_buses); b++) {
			struct header_bus *bus = &header_buses[b];

			if (strcmp(bus->name, all[i]) != 0) {
				continue;
			}
			compiled = true;
			if (passed_through(bus->name) && start_device(bus->dev, bus->name)) {
				atomic_set(&bus->on, 1);
			}
		}
		if (!compiled && passed_through(all[i])) {
			/* "lpi2c5" -> CONFIG_EPP_LPI2C5 */
			printk("app: %s is passed through, but CONFIG_EPP_LPI2C%c is off\n",
			       all[i], all[i][5]);
		}
	}
}

static void header_report(void)
{
	for (size_t b = 0U; b < ARRAY_SIZE(header_buses); b++) {
		if (atomic_get(&header_buses[b].on)) {
			scan(header_buses[b].dev, header_buses[b].label);
		}
	}
#ifdef CONFIG_EPP_PCF8574
	if (passed_through("lpi2c6")) {
		printk("app: PCF8574 P0-P7 walk: %ld steps, %ld errors (ready after %ld s)\n",
		       (long)atomic_get(&pcf_steps), (long)atomic_get(&pcf_errors),
		       (long)atomic_get(&pcf_setup_s));
	}
#endif
}

/* ---- lpspi7: 40-pin header SPI ----------------------------------------- */

#ifdef CONFIG_EPP_LPSPI7
static const struct device *const header_spi = DEVICE_DT_GET(DT_NODELABEL(lpspi7));
static atomic_t spi_on;

#ifdef CONFIG_EPP_W5500
BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(w5500)), "CONFIG_EPP_W5500 needs w5500.overlay");

#define W5500_SPI_OP     (SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define W5500_SHAR       0x0009 /* MAC address, 6 bytes */
#define W5500_PHYCFGR    0x002e
#define W5500_VERSIONR   0x0039
#define W5500_CTRL_WRITE BIT(2) /* common register block, variable length */

static const struct spi_dt_spec w5500 = SPI_DT_SPEC_GET(DT_NODELABEL(w5500), W5500_SPI_OP);
static const struct gpio_dt_spec w5500_reset =
	GPIO_DT_SPEC_GET_OR(DT_NODELABEL(w5500), reset_gpios, {0});

/* W5500 SPI frame: 16-bit address, control byte, data */
static int w5500_access(uint16_t addr, bool write, uint8_t *data, size_t len)
{
	uint8_t hdr[3] = {addr >> 8, addr & 0xffU, write ? W5500_CTRL_WRITE : 0x00U};
	struct spi_buf tx_bufs[2] = {{.buf = hdr, .len = sizeof(hdr)}, {.buf = data, .len = len}};
	struct spi_buf rx_bufs[2] = {{.buf = NULL, .len = sizeof(hdr)}, {.buf = data, .len = len}};
	const struct spi_buf_set tx = {.buffers = tx_bufs, .count = write ? 2U : 1U};
	const struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2U};

	return write ? spi_write_dt(&w5500, &tx) : spi_transceive_dt(&w5500, &tx, &rx);
}

static void w5500_hw_reset(void)
{
	if ((w5500_reset.port == NULL) ||
	    (gpio_pin_configure_dt(&w5500_reset, GPIO_OUTPUT_ACTIVE) != 0)) {
		return;
	}
	k_msleep(1);
	gpio_pin_set_dt(&w5500_reset, 0);
	k_msleep(100); /* PLL lock after reset: max. 1 ms, link setup longer */
}

/* Version, PHY state and a MAC address register write and read back */
static void spi_test(uint32_t round)
{
	uint8_t version = 0U, phy = 0U;
	uint8_t mac[6] = {0x02, 0x00, 0x5e, 0x10, (uint8_t)(round >> 8), (uint8_t)round};
	uint8_t back[6] = {0};
	bool ok = (w5500_access(W5500_VERSIONR, false, &version, 1) == 0) &&
		  (w5500_access(W5500_PHYCFGR, false, &phy, 1) == 0);
	bool mac_ok = ok && (w5500_access(W5500_SHAR, true, mac, sizeof(mac)) == 0) &&
		      (w5500_access(W5500_SHAR, false, back, sizeof(back)) == 0) &&
		      (memcmp(mac, back, sizeof(mac)) == 0);

	if (!ok) {
		printk("app: header spi: W5500 not answering\n");
		return;
	}
	printk("app: header spi: W5500 version 0x%02x %s, link %s%s, MAC write/read %s\n",
	       version, (version == 0x04U) ? "OK" : "unexpected",
	       ((phy & BIT(0)) != 0U) ? "up " : "down",
	       ((phy & BIT(0)) != 0U) ? (((phy & BIT(1)) != 0U) ? "100M" : "10M") : "",
	       mac_ok ? "OK" : "FAILED");
}
#else
/* Loopback without chip select: MOSI (pin 31) to MISO (pin 29) */
static const struct spi_config loop_cfg = {
	.frequency = 1000000U,
	.operation = SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
};

static void spi_test(uint32_t round)
{
	uint8_t out[8] = {0x55, 0xaa, 0x00, 0xff, (uint8_t)round, (uint8_t)~round, 0x5a, 0xa5};
	uint8_t in[8] = {0};
	struct spi_buf tx_buf = {.buf = out, .len = sizeof(out)};
	struct spi_buf rx_buf = {.buf = in, .len = sizeof(in)};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1U};
	const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1U};
	int ret = spi_transceive(header_spi, &loop_cfg, &tx, &rx);

	if (ret != 0) {
		printk("app: header spi: transfer failed (%d)\n", ret);
	} else if (memcmp(out, in, sizeof(out)) == 0) {
		printk("app: header spi: loopback OK (8 bytes)\n");
	} else {
		printk("app: header spi: no loopback, read %02x %02x %02x %02x (jumper MOSI pin 31 "
		       "- MISO pin 29?)\n",
		       in[0], in[1], in[2], in[3]);
	}
}
#endif /* CONFIG_EPP_W5500 */

static void spi_start(void)
{
	if (!start_device(header_spi, "lpspi7")) {
		return;
	}
#ifdef CONFIG_EPP_W5500
	w5500_hw_reset();
#endif
	atomic_set(&spi_on, 1);
}

static void spi_report(uint32_t round)
{
	if (atomic_get(&spi_on)) {
		spi_test(round);
	}
}
#else
static void spi_start(void)
{
	printk("app: lpspi7 is passed through, but CONFIG_EPP_LPSPI7 is off\n");
}

static void spi_report(uint32_t round)
{
	ARG_UNUSED(round);
}
#endif /* CONFIG_EPP_LPSPI7 */

/* ---- passthrough list -------------------------------------------------- */

/*
 * Wait for the passthrough list from Dom0 (written by
 * epp-v2-dom0less-setup once Dom0 has booted), then start the listed I2C
 * buses. The header pins work only from then on as well: Dom0 sets their pin
 * mux when its Linux boots.
 */
static void passthrough_thread(void *p1, void *p2, void *p3)
{
	char buf[sizeof(passthrough)];
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = epp_xs_init(K_SECONDS(120));
	if (ret != 0) {
		snprintf(passthrough, sizeof(passthrough), "(no xenstore: %d)", ret);
		return;
	}
	while (epp_xs_read("epp/passthrough", buf, sizeof(buf)) < 0) {
		k_msleep(1000);
	}
	strcpy(passthrough, buf);

	if (passed_through("lpi2c4")) {
		display_start();
	}
	if (passed_through("lpspi7")) {
		spi_start();
	}
	header_start();
#ifdef CONFIG_EPP_PCF8574
	/* On lpi2c6 (pins 3/5); does not return */
	if (passed_through("lpi2c6")) {
		pcf8574_walk();
	}
#endif
}

K_THREAD_DEFINE(pt_thread, 4096, passthrough_thread, NULL, NULL, NULL, 7, 0, 0);

/* ---- main: LED and network --------------------------------------------- */

static void print_net(struct net_if *iface)
{
	struct net_linkaddr *ll = net_if_get_link_addr(iface);
	struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	char addr[NET_IPV4_ADDR_LEN];

	if (ip != NULL) {
		net_addr_ntop(NET_AF_INET, ip, addr, sizeof(addr));
	} else {
		snprintf(addr, sizeof(addr), "none");
	}

	printk("app: net: carrier %s, mac %02x:%02x:%02x:%02x:%02x:%02x, ipv4 %s\n",
	       net_if_is_carrier_ok(iface) ? "on" : "off", ll->addr[0], ll->addr[1], ll->addr[2],
	       ll->addr[3], ll->addr[4], ll->addr[5], addr);
}

int main(void)
{
	struct net_if *iface = net_if_get_default();
	bool leds_ok = true;

	for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i]) ||
		    (gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE) != 0)) {
			leds_ok = false;
		}
	}
	printk("app: LED %s, network %s\n", leds_ok ? "ready" : "NOT usable",
	       (iface != NULL) ? "ready" : "NOT available");

	if (iface != NULL) {
		net_dhcpv4_start(iface);
	}

	for (uint32_t round = 0U;; round++) {
		size_t s = round % ARRAY_SIZE(steps);

		if (leds_ok) {
			for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
				gpio_pin_set_dt(&leds[i], (steps[s].on >> i) & 1U);
			}
		}
		if ((round % 10U) == 0U) {
			printk("app: [%u] LED %s, passthrough %s\n", round / 10U,
			       leds_ok ? "cycling" : "off", passthrough);
			if (iface != NULL) {
				print_net(iface);
			}
			display_report(round / 10U);
			header_report();
			spi_report(round / 10U);
		}
		k_msleep(1000);
	}

	return 0;
}
