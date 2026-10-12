/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part lpspi7 with the W5500 as network interface (CONFIG_EPP_W5500_ETH,
 * extras/w5500-eth.overlay): Zephyr's W5500 Ethernet driver; LPSPI7 and the
 * driver start at boot (pins set by U-Boot). DHCP on it, its state every
 * 10 s.
 */

#include <zephyr/net/net_if.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/printk.h>

#include "part.h"

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(w5500_eth)),
	     "CONFIG_EPP_W5500_ETH needs extras/w5500-eth.overlay");

static struct net_if *w5500_iface(void)
{
	return net_if_lookup_by_dev(DEVICE_DT_GET(DT_NODELABEL(w5500_eth)));
}

static void w5500_eth_start(void)
{
	if (w5500_iface() != NULL) {
		net_dhcpv4_start(w5500_iface());
	}
}

static void w5500_eth_report(uint32_t round)
{
	ARG_UNUSED(round);
	if (w5500_iface() != NULL) {
		demo_print_net(w5500_iface(), "w5500");
	} else {
		printk("app: w5500: no network interface (driver not ready)\n");
	}
}

DEMO_PART(p4_lpspi7, .option = "lpspi7", .start = w5500_eth_start, .report = w5500_eth_report);
