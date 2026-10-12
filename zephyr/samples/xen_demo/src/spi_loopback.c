/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part lpspi7 (CONFIG_EPP_LPSPI7, without a W5500): the SPI of the 40-pin
 * header. Every 10 s a loopback test without chip select: a jumper from
 * MOSI (pin 31) to MISO (pin 29).
 */

#include <string.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "part.h"

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(lpspi7));
static atomic_t on;

static const struct spi_config loop_cfg = {
	.frequency = 1000000U,
	.operation = SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
};

static void loopback_start(void)
{
	if (demo_start_device(bus, "lpspi7")) {
		atomic_set(&on, 1);
	}
}

static void loopback_report(uint32_t round)
{
	uint8_t out[8] = {0x55, 0xaa, 0x00, 0xff, (uint8_t)round, (uint8_t)~round, 0x5a, 0xa5};
	uint8_t in[8] = {0};
	struct spi_buf tx_buf = {.buf = out, .len = sizeof(out)};
	struct spi_buf rx_buf = {.buf = in, .len = sizeof(in)};
	const struct spi_buf_set tx = {.buffers = &tx_buf, .count = 1U};
	const struct spi_buf_set rx = {.buffers = &rx_buf, .count = 1U};
	int ret;

	if (!atomic_get(&on)) {
		return;
	}
	ret = spi_transceive(bus, &loop_cfg, &tx, &rx);
	if (ret != 0) {
		printk("app: header spi: transfer failed (%d)\n", ret);
	} else if (memcmp(out, in, sizeof(out)) == 0) {
		printk("app: header spi: loopback OK (8 bytes)\n");
	} else {
		printk("app: header spi: no loopback, read %02x %02x %02x %02x (jumper MOSI pin 31 "
		       "- MISO pin 29?)\n",
		       in[0], in[1], in[2], in[3]);
	}
}

DEMO_PART(p4_lpspi7, .option = "lpspi7", .start = loopback_start, .report = loopback_report);
