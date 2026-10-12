/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part PCF8574 on lpi2c6 (CONFIG_EPP_PCF8574, extras/pcf8574.overlay): a
 * PCF8574 I/O expander module at 0x20 on header pins 3/5, its pins P0-P7
 * driven as GPIOs. One pin high after the other (the others low), then all
 * high, then all low, 500 ms per step, every step read back: a high output
 * is the PCF8574's weak pull-up, so a pin pulled low from outside reads back
 * low and counts as an error.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "part.h"

BUILD_ASSERT(DT_NODE_HAS_PROP(DT_PATH(zephyr_user), pcf8574_gpios),
	     "CONFIG_EPP_PCF8574 needs extras/pcf8574.overlay (pcf8574 and zephyr,user nodes)");

/* PCF8574 P0-P7, from pcf8574-gpios of the zephyr,user node */
#define PCF_GPIO(node, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node, prop, idx),
static const struct gpio_dt_spec pins[] = {
	DT_FOREACH_PROP_ELEM(DT_PATH(zephyr_user), pcf8574_gpios, PCF_GPIO)};

static atomic_t steps;
static atomic_t errors;
static atomic_t ready_s; /* seconds until the PCF8574 answered */
static atomic_t on;

static void walk(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* The driver only writes pins configured as outputs */
	for (uint32_t s = 1U;; s++) {
		bool ok = true;

		for (size_t pin = 0U; ok && (pin < ARRAY_SIZE(pins)); pin++) {
			ok = (gpio_pin_configure_dt(&pins[pin], GPIO_OUTPUT_INACTIVE) == 0);
		}
		if (ok) {
			atomic_set(&ready_s, s);
			break;
		}
		k_msleep(1000);
	}

	for (uint32_t step = 0U;; step++) {
		uint32_t s = step % 10U;
		uint32_t out = (s < 8U) ? BIT(s) : ((s == 8U) ? 0xffU : 0x00U);
		uint32_t in = 0U;
		bool ok = true;

		for (size_t pin = 0U; pin < ARRAY_SIZE(pins); pin++) {
			ok = ok && (gpio_pin_set_dt(&pins[pin], (out >> pin) & 1U) == 0);
		}
		for (size_t pin = 0U; ok && (pin < ARRAY_SIZE(pins)); pin++) {
			int val = gpio_pin_get_dt(&pins[pin]);

			ok = (val >= 0);
			in |= (uint32_t)(val > 0) << pin;
		}
		if (!ok || (in != out)) {
			atomic_inc(&errors);
			printk("app: PCF8574 step %u: wrote 0x%02x read 0x%02x\n", s,
			       (unsigned int)out, (unsigned int)in);
		}
		atomic_inc(&steps);
		k_msleep(500);
	}
}

K_THREAD_DEFINE(pcf8574_thread, 2048, walk, NULL, NULL, NULL, 7, 0, SYS_FOREVER_MS);

static void pcf8574_start(void)
{
	/* After the bus (part p2_lpi2c6) */
	if (demo_start_device(DEVICE_DT_GET(DT_NODELABEL(lpi2c6)), "lpi2c6") &&
	    demo_start_device(pins[0].port, "PCF8574")) {
		atomic_set(&on, 1);
		k_thread_start(pcf8574_thread);
	}
}

static void pcf8574_report(uint32_t round)
{
	ARG_UNUSED(round);
	if (atomic_get(&on)) {
		printk("app: PCF8574 P0-P7 walk: %ld steps, %ld errors (ready after %ld s)\n",
		       (long)atomic_get(&steps), (long)atomic_get(&errors),
		       (long)atomic_get(&ready_s));
	}
}

DEMO_PART(p3_pcf8574, .option = "lpi2c6", .start = pcf8574_start, .report = pcf8574_report);
