/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr Xen DomU with the shared Ethernet (netfront, DHCP) and two devices
 * passed through by Xen: GPIO2 cycles the FRDM-IMX95 RGB LED D19 (red,
 * green, blue, white, off) every second; every 10 s the network state is
 * printed and the I2C bus of the 40-pin header (LPI2C6, pins 3/5) is
 * scanned. Answers pings from the network.
 *
 * With the optional PCF8574 test module enabled in app.overlay, its pins
 * P0-P7 are driven as Zephyr GPIOs: one pin after the other goes high, then
 * all high, then all low, every step read back.
 */

#include <errno.h>
#include <stdio.h>
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

static const struct device *const header_i2c = DEVICE_DT_GET(DT_NODELABEL(lpi2c6));

static const struct {
	const char *name;
	uint8_t on; /* bit 0 red, 1 green, 2 blue */
} steps[] = {
	{"red", 0x1}, {"green", 0x2}, {"blue", 0x4}, {"white", 0x7}, {"off", 0x0},
};

#define HAS_PCF8574 DT_NODE_HAS_PROP(DT_PATH(zephyr_user), pcf8574_gpios)

#if HAS_PCF8574
/* PCF8574 P0-P7, from pcf8574-gpios of the zephyr,user node */
#define PCF_GPIO(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec pcf_pins[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pcf8574_gpios, PCF_GPIO)};

/* PCF8574 walk, counted by the thread, printed by main */
static atomic_t pcf_steps;
static atomic_t pcf_errors;
static atomic_t pcf_setup_s; /* seconds until the PCF8574 answered */

/*
 * One pin high after the other (the others low), then all high, then all
 * low, 500 ms per step. Every step is read back from the pins: a high
 * output is the PCF8574's weak pull-up, so a pin pulled low from outside
 * reads back low and counts as an error.
 */
static void pcf8574_walk(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (!gpio_is_ready_dt(&pcf_pins[0])) {
		printk("app: PCF8574 NOT ready\n");
		return;
	}

	/*
	 * The driver only writes pins configured as outputs. Configuring writes
	 * to the PCF8574, which fails until Dom0 has booted: Xen starts this
	 * DomU together with Dom0, and Dom0 sets the pin mux of the header pins
	 * (pinctrl hog) only when its Linux starts. Retry until then.
	 */
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

K_THREAD_DEFINE(pcf8574_thread, 4096, pcf8574_walk, NULL, NULL, NULL, 7, 0, 0);
#endif /* HAS_PCF8574 */

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

/* Print the addresses that answer on the header bus */
static void scan_header(void)
{
	int found = 0;

	printk("app: header i2c (J19 pins 3/5):");
	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		/* Address only, no data: the device answers with ACK or not */
		int ret = i2c_write(header_i2c, NULL, 0, addr);

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

int main(void)
{
	struct net_if *iface = net_if_get_default();
	bool leds_ok = true;
	bool header_ok = device_is_ready(header_i2c);

	for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i]) ||
		    (gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE) != 0)) {
			leds_ok = false;
		}
	}
	printk("app: LED %s, header I2C %s, network %s\n", leds_ok ? "ready" : "NOT usable",
	       header_ok ? "ready" : "NOT ready", (iface != NULL) ? "ready" : "NOT available");

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
			printk("app: [%u] LED %s\n", round / 10U, leds_ok ? "cycling" : "off");
			if (iface != NULL) {
				print_net(iface);
			}
			/* Fails until Dom0 has set the header pin mux (a few s) */
			if (header_ok) {
				scan_header();
			}
#if HAS_PCF8574
			printk("app: PCF8574 P0-P7 walk: %ld steps, %ld errors (ready after %ld s)\n",
			       (long)atomic_get(&pcf_steps), (long)atomic_get(&pcf_errors),
			       (long)atomic_get(&pcf_setup_s));
#endif
		}
		k_msleep(1000);
	}

	return 0;
}
