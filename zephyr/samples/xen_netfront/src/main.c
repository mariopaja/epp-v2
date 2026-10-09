/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Step 2/3 of the Zephyr Xen network frontend: bring up the netfront
 * interface, get an IPv4 address by DHCP over the Dom0 bridge and print
 * the state every 5 s. Answer pings from the network.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	struct net_if *iface = net_if_get_default();
	char addr[NET_IPV4_ADDR_LEN];

	if (iface == NULL) {
		printk("xen_netfront: no network interface\n");
		return 0;
	}

	net_dhcpv4_start(iface);

	while (true) {
		struct net_linkaddr *ll = net_if_get_link_addr(iface);
		struct net_in_addr *ip = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

		if (ip != NULL) {
			net_addr_ntop(NET_AF_INET, ip, addr, sizeof(addr));
		} else {
			snprintf(addr, sizeof(addr), "none");
		}

		printk("xen_netfront: carrier %s, mac %02x:%02x:%02x:%02x:%02x:%02x, ipv4 %s\n",
		       net_if_is_carrier_ok(iface) ? "on" : "off", ll->addr[0], ll->addr[1],
		       ll->addr[2], ll->addr[3], ll->addr[4], ll->addr[5], addr);
		k_msleep(5000);
	}

	return 0;
}
