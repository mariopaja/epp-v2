/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part lpuart7 (CONFIG_EPP_LPUART7): the UART of the 40-pin header. Every
 * 10 s a loopback test at 115200 baud (wire from TX pin 24 to RX pin 21):
 * every byte sent must come back within a few character times (one byte:
 * 87 us).
 */

#include <stdio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "part.h"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(lpuart7));
static atomic_t on;

static void uart_part_start(void)
{
	if (demo_start_device(uart, "lpuart7")) {
		atomic_set(&on, 1);
	}
}

static void uart_part_report(uint32_t round)
{
	char line[32];
	int len = snprintf(line, sizeof(line), "epp-v2 uart %u\r\n", round);
	unsigned char c;
	int ok = 0;

	if (!atomic_get(&on)) {
		return;
	}
	while (uart_poll_in(uart, &c) == 0) {
		/* drop old input */
	}
	(void)uart_err_check(uart);

	for (int i = 0; i < len; i++) {
		int64_t end = k_uptime_get() + 5;
		int got = -1;

		uart_poll_out(uart, line[i]);
		while ((got = uart_poll_in(uart, &c)) != 0) {
			if (k_uptime_get() > end) {
				break;
			}
		}
		if ((got != 0) || (c != (unsigned char)line[i])) {
			break;
		}
		ok++;
	}

	if (ok == len) {
		printk("app: header uart (pins 24/21): loopback OK (%d bytes, 115200 baud)\n", len);
	} else {
		printk("app: header uart (pins 24/21): no loopback after %d of %d bytes, errors 0x%x "
		       "(wire TX pin 24 - RX pin 21?)\n",
		       ok, len, uart_err_check(uart));
	}
}

DEMO_PART(p6_lpuart7, .option = "lpuart7", .start = uart_part_start, .report = uart_part_report);
