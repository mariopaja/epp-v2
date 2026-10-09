/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr Xen DomU with the shared Ethernet (netfront, DHCP) and two devices
 * passed through by Xen: GPIO2 cycles the FRDM-IMX95 RGB LED D19 (red,
 * green, blue, white, off) every second; every 10 s the network state is
 * printed and LPI2C4 is checked (bus scan, IT6263 chip ID, PCA9632 register
 * write and read back). Answers pings from the network.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define IT6263_HDMI_ADDR 0x4c
#define PCA9632_ADDR     0x62
#define PCA9632_PWM0     0x02

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue), gpios),
};

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(lpi2c4));

static const struct {
	const char *name;
	uint8_t on; /* bit 0 red, 1 green, 2 blue */
} steps[] = {
	{"red", 0x1}, {"green", 0x2}, {"blue", 0x4}, {"white", 0x7}, {"off", 0x0},
};

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

static void check_i2c(uint8_t pattern)
{
	static const uint8_t hdmi_id[4] = {0x01, 0xca, 0x13, 0x76};
	uint8_t reg = 0x00;
	uint8_t id[4];
	uint8_t old, back;
	int found = 0;

	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		/* Address only, no data: the device answers with ACK or not */
		if (i2c_write(i2c, NULL, 0, addr) == 0) {
			found++;
		}
	}

	bool id_ok = (i2c_write_read(i2c, IT6263_HDMI_ADDR, &reg, 1, id, sizeof(id)) == 0) &&
		     (memcmp(id, hdmi_id, sizeof(id)) == 0);
	bool rw_ok = (i2c_reg_read_byte(i2c, PCA9632_ADDR, PCA9632_PWM0, &old) == 0) &&
		     (i2c_reg_write_byte(i2c, PCA9632_ADDR, PCA9632_PWM0, pattern) == 0) &&
		     (i2c_reg_read_byte(i2c, PCA9632_ADDR, PCA9632_PWM0, &back) == 0) &&
		     (back == pattern) &&
		     (i2c_reg_write_byte(i2c, PCA9632_ADDR, PCA9632_PWM0, old) == 0);

	printk("app: i2c: %d devices, IT6263 ID %s, PCA9632 write/read 0x%02x %s\n", found,
	       id_ok ? "OK" : "FAILED", pattern, rw_ok ? "OK" : "FAILED");
}

int main(void)
{
	struct net_if *iface = net_if_get_default();
	bool leds_ok = true;
	bool i2c_ok = device_is_ready(i2c);

	for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i]) ||
		    (gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE) != 0)) {
			leds_ok = false;
		}
	}
	printk("app: LED %s, I2C %s, network %s\n", leds_ok ? "ready" : "NOT usable",
	       i2c_ok ? "ready" : "NOT ready", (iface != NULL) ? "ready" : "NOT available");

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
			if (i2c_ok) {
				check_i2c(((round / 10U) & 1U) ? 0xa5 : 0x5a);
			}
		}
		k_msleep(1000);
	}

	return 0;
}
