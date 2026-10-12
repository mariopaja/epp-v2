/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr Xen DomU with the shared Ethernet (netfront, DHCP) and the devices
 * Xen passes through: GPIO2 cycles the FRDM-IMX95 RGB LED D19 (red, green,
 * blue, white, off) every second. Which I2C bus is passed through as well
 * is selected in Dom0 (epp-passthrough) and listed by Dom0 in xenstore
 * (epp/passthrough, e.g. "gpio2,lpi2c6"); only that bus is started. The
 * parts for the buses are compiled in with Kconfig (see Kconfig):
 *
 *   CONFIG_EPP_LPI2C4   lpi2c4, display bus: scan, IT6263 chip ID, PCA9632
 *                       register write and read back
 *   CONFIG_EPP_LPI2C6   lpi2c6, 40-pin header (pins 3/5): scan
 *   CONFIG_EPP_PCF8574  PCF8574 module on the header: its pins P0-P7 go high
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

#if defined(CONFIG_EPP_LPI2C4) || defined(CONFIG_EPP_LPI2C6)
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
#endif

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

/* ---- lpi2c6: 40-pin header -------------------------------------------- */

#ifdef CONFIG_EPP_PCF8574
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_PATH(zephyr_user), pcf8574_gpios),
	     "CONFIG_EPP_PCF8574 needs the pcf8574 and zephyr,user nodes in app.overlay");

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

#ifdef CONFIG_EPP_LPI2C6
static const struct device *const header_i2c = DEVICE_DT_GET(DT_NODELABEL(lpi2c6));
static atomic_t header_on;

/* Runs in the passthrough thread; with the PCF8574 test it does not return */
static void header_start(void)
{
	if (!start_device(header_i2c, "lpi2c6")) {
		return;
	}
	atomic_set(&header_on, 1);
#ifdef CONFIG_EPP_PCF8574
	pcf8574_walk();
#endif
}

static void header_report(void)
{
	if (!atomic_get(&header_on)) {
		return;
	}
	scan(header_i2c, "header i2c (J19 pins 3/5)");
#ifdef CONFIG_EPP_PCF8574
	printk("app: PCF8574 P0-P7 walk: %ld steps, %ld errors (ready after %ld s)\n",
	       (long)atomic_get(&pcf_steps), (long)atomic_get(&pcf_errors),
	       (long)atomic_get(&pcf_setup_s));
#endif
}
#else
static void header_start(void)
{
	printk("app: lpi2c6 is passed through, but CONFIG_EPP_LPI2C6 is off\n");
}

static void header_report(void)
{
}
#endif /* CONFIG_EPP_LPI2C6 */

/* ---- passthrough list -------------------------------------------------- */

/*
 * Wait for the passthrough list from Dom0 (written by
 * epp-v2-dom0less-setup once Dom0 has booted), then start the listed I2C
 * bus. The header pins work only from then on as well: Dom0 sets their pin
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
	if (passed_through("lpi2c6")) {
		header_start();
	}
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
		}
		k_msleep(1000);
	}

	return 0;
}
