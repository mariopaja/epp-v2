/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C passthrough test for the Zephyr Xen DomU: on LPI2C4, passed through by
 * Xen, scan the bus, read the chip IDs of the IT6263 LVDS-to-HDMI bridge and
 * write and read back a register of the PCA9632 LED driver. Repeats every
 * 10 s.
 */

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define IT6263_HDMI_ADDR 0x4c
#define IT6263_LVDS_ADDR 0x33
#define PCA9632_ADDR     0x62
#define PCA9632_MODE1    0x00
#define PCA9632_MODE2    0x01
#define PCA9632_PWM0     0x02

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(lpi2c4));

static void scan(void)
{
	printk("xen_i2c: scan:");
	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		/* Address only, no data: the device answers with ACK or not */
		if (i2c_write(bus, NULL, 0, addr) == 0) {
			printk(" 0x%02x", addr);
		}
	}
	printk("\n");
}

static void read_id(const char *name, uint16_t addr, const uint8_t expect[4])
{
	uint8_t reg = 0x00;
	uint8_t id[4];
	int ret = i2c_write_read(bus, addr, &reg, 1, id, sizeof(id));

	if (ret != 0) {
		printk("xen_i2c: %s (0x%02x): read failed (%d)\n", name, addr, ret);
		return;
	}
	printk("xen_i2c: %s (0x%02x): vendor %02x %02x device %02x %02x %s\n", name, addr, id[0],
	       id[1], id[2], id[3], (memcmp(id, expect, sizeof(id)) == 0) ? "OK" : "unexpected");
}

static void pca9632_test(uint8_t pattern)
{
	uint8_t mode1, mode2, old, back;

	if ((i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_MODE1, &mode1) != 0) ||
	    (i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_MODE2, &mode2) != 0) ||
	    (i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_PWM0, &old) != 0) ||
	    (i2c_reg_write_byte(bus, PCA9632_ADDR, PCA9632_PWM0, pattern) != 0) ||
	    (i2c_reg_read_byte(bus, PCA9632_ADDR, PCA9632_PWM0, &back) != 0) ||
	    (i2c_reg_write_byte(bus, PCA9632_ADDR, PCA9632_PWM0, old) != 0)) {
		printk("xen_i2c: PCA9632 (0x%02x): access failed\n", PCA9632_ADDR);
		return;
	}
	printk("xen_i2c: PCA9632 (0x%02x): MODE1 0x%02x MODE2 0x%02x, PWM0 wrote 0x%02x read "
	       "0x%02x %s\n",
	       PCA9632_ADDR, mode1, mode2, pattern, back, (back == pattern) ? "OK" : "MISMATCH");
}

int main(void)
{
	static const uint8_t hdmi_id[4] = {0x01, 0xca, 0x13, 0x76};
	static const uint8_t lvds_id[4] = {0x15, 0xca, 0x61, 0x62};

	if (!device_is_ready(bus)) {
		printk("xen_i2c: LPI2C4 not ready\n");
		return 0;
	}

	for (uint32_t round = 0U;; round++) {
		printk("xen_i2c: [%u]\n", round);
		scan();
		read_id("IT6263 HDMI", IT6263_HDMI_ADDR, hdmi_id);
		read_id("IT6263 LVDS", IT6263_LVDS_ADDR, lvds_id);
		pca9632_test((round & 1U) ? 0xa5 : 0x5a);
		k_msleep(10000);
	}

	return 0;
}
