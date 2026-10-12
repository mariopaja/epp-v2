/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * A part of the demo: a device Xen can pass through, with its test. Each
 * part file defines one or more parts with DEMO_PART(); CMakeLists.txt
 * compiles a part file only when its Kconfig option is on. main.c starts the
 * parts whose passthrough option Dom0 lists and calls their functions.
 */

#ifndef XEN_DEMO_PART_H_
#define XEN_DEMO_PART_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/iterable_sections.h>

struct demo_part {
	/* Passthrough option that enables the part (epp-passthrough) */
	const char *option;
	/* Once Dom0 lists the option; runs in the passthrough thread */
	void (*start)(void);
	/* Every second / every 10 s in the main thread; may be NULL */
	void (*every_second)(uint32_t second);
	void (*report)(uint32_t round);
};

/* Parts run in the order of @p id (sorted by name) */
#define DEMO_PART(id, ...) static const STRUCT_SECTION_ITERABLE(demo_part, id) = {__VA_ARGS__}

/* Helpers in main.c */

/* Initialize a device marked zephyr,deferred-init (once passed through) */
bool demo_start_device(const struct device *dev, const char *name);

/* One line with the state, MAC and IPv4 address of @p iface */
void demo_print_net(struct net_if *iface, const char *label);

/* Helper in i2c_scan.c */

/* One line with the addresses that answer on I2C bus @p bus */
void demo_i2c_scan(const struct device *bus, const char *label);

#endif /* XEN_DEMO_PART_H_ */
