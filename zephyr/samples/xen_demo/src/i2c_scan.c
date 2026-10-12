/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C bus scan, shared by the I2C parts.
 */

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#include "part.h"

void demo_i2c_scan(const struct device *bus, const char *label)
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
