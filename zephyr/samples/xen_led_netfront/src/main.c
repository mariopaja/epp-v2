/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr Xen DomU with both the shared Ethernet (netfront, DHCP) and the
 * passed-through GPIO2: cycle the FRDM-IMX95 RGB LED D19 (red, green, blue,
 * white, off) every second and print the network state every 5 s. Answer
 * pings from the network.
 */

#include <stdio.h>
#include <zephyr/drivers/gpio.h>
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

static void print_net(struct net_if *iface, const char *led)
{
	struct net_linkaddr *ll = net_if_get_link_addr(iface);
	struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	char addr[NET_IPV4_ADDR_LEN];

	if (ip != NULL) {
		net_addr_ntop(NET_AF_INET, ip, addr, sizeof(addr));
	} else {
		snprintf(addr, sizeof(addr), "none");
	}

	printk("xen_led_netfront: carrier %s, mac %02x:%02x:%02x:%02x:%02x:%02x, ipv4 %s, "
	       "D19 %s\n",
	       net_if_is_carrier_ok(iface) ? "on" : "off", ll->addr[0], ll->addr[1], ll->addr[2],
	       ll->addr[3], ll->addr[4], ll->addr[5], addr, led);
}

int main(void)
{
	struct net_if *iface = net_if_get_default();
	bool leds_ok = true;

	for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i]) ||
		    (gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE) != 0)) {
			printk("xen_led_netfront: LED %u not usable\n", (unsigned int)i);
			leds_ok = false;
		}
	}

	if (iface == NULL) {
		printk("xen_led_netfront: no network interface\n");
	} else {
		net_dhcpv4_start(iface);
	}

	for (uint32_t round = 0U;; round++) {
		size_t s = round % ARRAY_SIZE(steps);

		if (leds_ok) {
			for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
				gpio_pin_set_dt(&leds[i], (steps[s].on >> i) & 1U);
			}
		}
		if ((iface != NULL) && (s == 0U)) {
			print_net(iface, leds_ok ? "cycling" : "unusable");
		}
		k_msleep(1000);
	}

	return 0;
}
