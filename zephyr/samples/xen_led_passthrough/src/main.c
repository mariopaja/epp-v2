/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO passthrough test for the Zephyr Xen DomU: cycle the FRDM-IMX95 RGB
 * LED D19 (red, green, blue, white, off) on GPIO2, passed through by Xen.
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_red), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_green), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(led_blue), gpios),
};

static const struct {
	const char *name;
	uint8_t on; /* bit 0 red, 1 green, 2 blue */
} steps[] = {
	{"red", 0x1}, {"green", 0x2}, {"blue", 0x4}, {"white", 0x7}, {"off", 0x0},
};

int main(void)
{
	for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i]) ||
		    (gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE) != 0)) {
			printk("xen_led: LED %u not usable\n", (unsigned int)i);
			return 0;
		}
	}

	for (uint32_t round = 0U;; round++) {
		size_t s = round % ARRAY_SIZE(steps);

		for (size_t i = 0U; i < ARRAY_SIZE(leds); i++) {
			gpio_pin_set_dt(&leds[i], (steps[s].on >> i) & 1U);
		}
		gpio_port_value_t in = 0U;

		(void)gpio_port_get_raw(leds[0].port, &in);
		printk("xen_led: [%u] D19 %s (PDIR 0x%08x: IO13 %u IO04 %u IO12 %u)\n", round,
		       steps[s].name, (unsigned int)in, (unsigned int)((in >> 13) & 1U),
		       (unsigned int)((in >> 4) & 1U), (unsigned int)((in >> 12) & 1U));
		k_msleep(1000);
	}

	return 0;
}
