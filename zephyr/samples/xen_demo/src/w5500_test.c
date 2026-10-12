/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part lpspi7 with a W5500 (CONFIG_EPP_W5500, extras/w5500.overlay): a
 * WIZnet W5500 Ethernet module on the header SPI, register test without
 * Ethernet driver. Every 10 s its version (0x04), PHY state and a write and
 * read back of its MAC address register.
 */

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "part.h"

BUILD_ASSERT(DT_NODE_EXISTS(DT_NODELABEL(w5500)), "CONFIG_EPP_W5500 needs extras/w5500.overlay");

#define W5500_SPI_OP     (SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB)
#define W5500_SHAR       0x0009 /* MAC address, 6 bytes */
#define W5500_PHYCFGR    0x002e
#define W5500_VERSIONR   0x0039
#define W5500_CTRL_WRITE BIT(2) /* common register block, variable length */

static const struct spi_dt_spec w5500 = SPI_DT_SPEC_GET(DT_NODELABEL(w5500), W5500_SPI_OP);
static const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(w5500), reset_gpios, {0});
static atomic_t on;

/* W5500 SPI frame: 16-bit address, control byte, data */
static int w5500_access(uint16_t addr, bool write, uint8_t *data, size_t len)
{
	uint8_t hdr[3] = {addr >> 8, addr & 0xffU, write ? W5500_CTRL_WRITE : 0x00U};
	struct spi_buf tx_bufs[2] = {{.buf = hdr, .len = sizeof(hdr)}, {.buf = data, .len = len}};
	struct spi_buf rx_bufs[2] = {{.buf = NULL, .len = sizeof(hdr)}, {.buf = data, .len = len}};
	const struct spi_buf_set tx = {.buffers = tx_bufs, .count = write ? 2U : 1U};
	const struct spi_buf_set rx = {.buffers = rx_bufs, .count = 2U};

	return write ? spi_write_dt(&w5500, &tx) : spi_transceive_dt(&w5500, &tx, &rx);
}

static void w5500_start(void)
{
	if (!demo_start_device(w5500.bus, "lpspi7")) {
		return;
	}
	if ((reset.port != NULL) && (gpio_pin_configure_dt(&reset, GPIO_OUTPUT_ACTIVE) == 0)) {
		k_msleep(1);
		gpio_pin_set_dt(&reset, 0);
		k_msleep(100); /* PLL lock after reset: max. 1 ms, link setup longer */
	}
	atomic_set(&on, 1);
}

static void w5500_report(uint32_t round)
{
	uint8_t version = 0U, phy = 0U;
	uint8_t mac[6] = {0x02, 0x00, 0x5e, 0x10, (uint8_t)(round >> 8), (uint8_t)round};
	uint8_t back[6] = {0};

	if (!atomic_get(&on)) {
		return;
	}

	bool ok = (w5500_access(W5500_VERSIONR, false, &version, 1) == 0) &&
		  (w5500_access(W5500_PHYCFGR, false, &phy, 1) == 0);
	bool mac_ok = ok && (w5500_access(W5500_SHAR, true, mac, sizeof(mac)) == 0) &&
		      (w5500_access(W5500_SHAR, false, back, sizeof(back)) == 0) &&
		      (memcmp(mac, back, sizeof(mac)) == 0);

	if (!ok) {
		printk("app: header spi: W5500 not answering\n");
		return;
	}
	printk("app: header spi: W5500 version 0x%02x %s, link %s%s, MAC write/read %s\n",
	       version, (version == 0x04U) ? "OK" : "unexpected",
	       ((phy & BIT(0)) != 0U) ? "up " : "down",
	       ((phy & BIT(0)) != 0U) ? (((phy & BIT(1)) != 0U) ? "100M" : "10M") : "",
	       mac_ok ? "OK" : "FAILED");
}

DEMO_PART(p4_lpspi7, .option = "lpspi7", .start = w5500_start, .report = w5500_report);
