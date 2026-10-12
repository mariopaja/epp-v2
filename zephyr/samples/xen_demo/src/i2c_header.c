/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Parts lpi2c5, lpi2c6, lpi2c7 (CONFIG_EPP_LPI2C5/6/7): the I2C buses of
 * the 40-pin header J19, pins 27/28, 3/5 and 31/26. Every 10 s a scan.
 */

#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "part.h"

/* One part per bus, compiled in when its Kconfig option is on */
#define HEADER_I2C(n, pins)                                                                        \
	static const struct device *const bus##n = DEVICE_DT_GET(DT_NODELABEL(lpi2c##n));         \
	static atomic_t on##n;                                                                     \
	static void start##n(void)                                                                 \
	{                                                                                          \
		if (demo_start_device(bus##n, "lpi2c" #n)) {                                       \
			atomic_set(&on##n, 1);                                                     \
		}                                                                                  \
	}                                                                                          \
	static void report##n(uint32_t round)                                                      \
	{                                                                                          \
		ARG_UNUSED(round);                                                                 \
		if (atomic_get(&on##n)) {                                                          \
			demo_i2c_scan(bus##n, "header i2c (J19 pins " pins ")");                   \
		}                                                                                  \
	}                                                                                          \
	DEMO_PART(p2_lpi2c##n, .option = "lpi2c" #n, .start = start##n, .report = report##n);

IF_ENABLED(CONFIG_EPP_LPI2C5, (HEADER_I2C(5, "27/28")))
IF_ENABLED(CONFIG_EPP_LPI2C6, (HEADER_I2C(6, "3/5")))
IF_ENABLED(CONFIG_EPP_LPI2C7, (HEADER_I2C(7, "31/26")))
