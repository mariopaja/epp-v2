/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part lpi2c4 (CONFIG_EPP_LPI2C4): the display bus. Every 10 s a scan, the
 * IT6263 chip ID (0x4c) and a write and read back of a PCA9632 register
 * (0x62).
 */

#include <string.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "part.h"

#define IT6263_HDMI_ADDR 0x4c
#define PCA9632_ADDR     0x62
#define PCA9632_PWM0     0x02

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(lpi2c4));
static atomic_t on;

static void display_start(void)
{
	if (demo_start_device(bus, "lpi2c4")) {
		atomic_set(&on, 1);
	}
}

static void display_report(uint32_t round)
{
	static const uint8_t hdmi_id[4] = {0x01, 0xca, 0x13, 0x76};
	uint8_t pattern = (round & 1U) ? 0xa5 : 0x5a;
	uint8_t reg = 0x00;
	uint8_t id[4];
	uint8_t old, back;

	if (!atomic_get(&on)) {
		return;
	}
	demo_i2c_scan(bus, "display i2c");

	bool id_ok = (i2c_write_read(bus, IT6263_HDMI_ADDR, &reg, 1, id, sizeof(id)) == 0) &&
		     (memcmp(id, hdmi_id, sizeof(id)) == 0);
	bool rw_ok = (i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_PWM0, &old) == 0) &&
		     (i2c_reg_write_byte(bus, PCA9632_ADDR, PCA9632_PWM0, pattern) == 0) &&
		     (i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_PWM0, &back) == 0) &&
		     (back == pattern) &&
		     (i2c_reg_write_byte(bus, PCA9632_ADDR, PCA9632_PWM0, old) == 0);

	printk("app: display i2c: IT6263 ID %s, PCA9632 write/read 0x%02x %s\n",
	       id_ok ? "OK" : "FAILED", pattern, rw_ok ? "OK" : "FAILED");
}

DEMO_PART(p1_lpi2c4, .option = "lpi2c4", .start = display_start, .report = display_report);
