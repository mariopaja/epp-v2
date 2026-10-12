/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * epp-v2 Zephyr DomU demo: the shared Ethernet (netfront, DHCP) and the
 * devices Xen passes through. GPIO2 is always passed through: the RGB LED
 * D19 cycles red, green, blue, white, off. The other devices are selected in
 * Dom0 (epp-passthrough); Dom0 lists them in xenstore (epp/passthrough, e.g.
 * "gpio2,lpi2c6,flexcan2"), and only their parts are started (see part.h and
 * the part files). Every 10 s the state of the network and of every part is
 * printed. Answers pings from the network.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <epp/xen/xenstore.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "part.h"

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
static char passthrough[96] = "(waiting for Dom0)";

bool demo_start_device(const struct device *dev, const char *name)
{
	int ret = device_init(dev);

	if ((ret != 0) && (ret != -EALREADY)) {
		printk("app: %s: init failed (%d)\n", name, ret);
		return false;
	}
	return true;
}

void demo_print_net(struct net_if *iface, const char *label)
{
	struct net_linkaddr *ll = net_if_get_link_addr(iface);
	struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	char addr[NET_IPV4_ADDR_LEN];

	if (ip != NULL) {
		net_addr_ntop(NET_AF_INET, ip, addr, sizeof(addr));
	} else {
		snprintf(addr, sizeof(addr), "none");
	}

	printk("app: %s: carrier %s, mac %02x:%02x:%02x:%02x:%02x:%02x, ipv4 %s\n", label,
	       net_if_is_carrier_ok(iface) ? "on" : "off", ll->addr[0], ll->addr[1], ll->addr[2],
	       ll->addr[3], ll->addr[4], ll->addr[5], addr);
}

/*
 * Wait for the passthrough list from Dom0 (written by epp-v2-dom0less-setup
 * once Dom0 has booted), then start the parts of the listed options. The
 * header pins work only from then on as well: Dom0 sets their pin mux when
 * its Linux boots.
 */
static void passthrough_thread(void *p1, void *p2, void *p3)
{
	char list[sizeof(passthrough)];
	char *save = NULL;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = epp_xs_init(K_SECONDS(120));
	if (ret != 0) {
		snprintf(passthrough, sizeof(passthrough), "(no xenstore: %d)", ret);
		return;
	}
	while (epp_xs_read("epp/passthrough", list, sizeof(list)) < 0) {
		k_msleep(1000);
	}
	strcpy(passthrough, list);

	for (char *option = strtok_r(list, ",", &save); option != NULL;
	     option = strtok_r(NULL, ",", &save)) {
		bool found = (strcmp(option, "gpio2") == 0); /* the LED, main() */

		STRUCT_SECTION_FOREACH(demo_part, part) {
			if (strcmp(part->option, option) == 0) {
				part->start();
				found = true;
			}
		}
		if (!found) {
			printk("app: %s is passed through, but its part is not compiled in "
			       "(Kconfig)\n",
			       option);
		}
	}
}

K_THREAD_DEFINE(pt_thread, 4096, passthrough_thread, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	/* netfront, not another network interface (W5500) */
	struct net_if *iface = net_if_lookup_by_dev(device_get_binding("xen-netfront"));
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

	for (uint32_t second = 0U;; second++) {
		size_t s = second % ARRAY_SIZE(steps);

		if (leds_ok) {
			for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
				gpio_pin_set_dt(&leds[i], (steps[s].on >> i) & 1U);
			}
		}
		STRUCT_SECTION_FOREACH(demo_part, part) {
			if (part->every_second != NULL) {
				part->every_second(second);
			}
		}
		if ((second % 10U) == 0U) {
			printk("app: [%u] LED %s, passthrough %s\n", second / 10U,
			       leds_ok ? "cycling" : "off", passthrough);
			if (iface != NULL) {
				demo_print_net(iface, "net");
			}
			STRUCT_SECTION_FOREACH(demo_part, part) {
				if (part->report != NULL) {
					part->report(second / 10U);
				}
			}
		}
		k_msleep(1000);
	}

	return 0;
}
