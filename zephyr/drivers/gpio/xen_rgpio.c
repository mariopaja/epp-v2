/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP RGPIO driver for a Zephyr Xen DomU (xenvm) that gets the GPIO
 * controller passed through by Xen. Pin mux and clocks are set up outside
 * the DomU, so unlike the SoC driver this one needs no SCMI, no pinctrl and
 * no MCUX headers. Inputs and outputs, no interrupts.
 */

#define DT_DRV_COMPAT epp_xen_rgpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(epp_xen_rgpio, CONFIG_GPIO_LOG_LEVEL);

/* RGPIO registers */
#define RGPIO_VERID 0x00
#define RGPIO_PARAM 0x04
#define RGPIO_PDOR  0x40
#define RGPIO_PSOR  0x44
#define RGPIO_PCOR  0x48
#define RGPIO_PTOR  0x4c
#define RGPIO_PDIR  0x50
#define RGPIO_PDDR  0x54
#define RGPIO_PIDR  0x58

/*
 * The GPIO API needs gpio_driver_config / gpio_driver_data first, so the
 * MMIO region is a named one (the unnamed DEVICE_MMIO_ROM/RAM must be first).
 */
struct xen_rgpio_config {
	struct gpio_driver_config common;
	DEVICE_MMIO_NAMED_ROM(reg_base);
};

struct xen_rgpio_data {
	struct gpio_driver_data common;
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_spinlock lock;
};

/* Used by the DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(dev)  ((const struct xen_rgpio_config *)(dev)->config)
#define DEV_DATA(dev) ((struct xen_rgpio_data *)(dev)->data)

static inline uint32_t rd(const struct device *dev, uint32_t off)
{
	return sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static inline void wr(const struct device *dev, uint32_t off, uint32_t val)
{
	sys_write32(val, DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static int xen_rgpio_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	struct xen_rgpio_data *data = dev->data;
	uint32_t bit = BIT(pin);
	k_spinlock_key_t key;

	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN | GPIO_SINGLE_ENDED)) != 0U) {
		/* Pad settings belong to the pin mux, set outside the DomU */
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);
	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			wr(dev, RGPIO_PSOR, bit);
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			wr(dev, RGPIO_PCOR, bit);
		}
		wr(dev, RGPIO_PDDR, rd(dev, RGPIO_PDDR) | bit);
	} else {
		wr(dev, RGPIO_PDDR, rd(dev, RGPIO_PDDR) & ~bit);
	}
	/* Input buffer on when used as input */
	if ((flags & GPIO_INPUT) != 0U) {
		wr(dev, RGPIO_PIDR, rd(dev, RGPIO_PIDR) & ~bit);
	}
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int xen_rgpio_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	*value = rd(dev, RGPIO_PDIR);
	return 0;
}

static int xen_rgpio_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	struct xen_rgpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	wr(dev, RGPIO_PDOR, (rd(dev, RGPIO_PDOR) & ~mask) | (value & mask));
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int xen_rgpio_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	wr(dev, RGPIO_PSOR, pins);
	return 0;
}

static int xen_rgpio_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	wr(dev, RGPIO_PCOR, pins);
	return 0;
}

static int xen_rgpio_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	wr(dev, RGPIO_PTOR, pins);
	return 0;
}

static int xen_rgpio_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);

	/* Not PCNS/PCNP: secure-only, reading them from the DomU aborts */
	LOG_INF("RGPIO at 0x%lx: VERID 0x%08x PARAM 0x%08x PDDR 0x%08x PDOR 0x%08x PDIR 0x%08x",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), rd(dev, RGPIO_VERID),
		rd(dev, RGPIO_PARAM), rd(dev, RGPIO_PDDR), rd(dev, RGPIO_PDOR), rd(dev, RGPIO_PDIR));
	return 0;
}

static DEVICE_API(gpio, xen_rgpio_api) = {
	.pin_configure = xen_rgpio_pin_configure,
	.port_get_raw = xen_rgpio_port_get_raw,
	.port_set_masked_raw = xen_rgpio_port_set_masked_raw,
	.port_set_bits_raw = xen_rgpio_port_set_bits_raw,
	.port_clear_bits_raw = xen_rgpio_port_clear_bits_raw,
	.port_toggle_bits = xen_rgpio_port_toggle_bits,
};

#define XEN_RGPIO_INIT(n)                                                                          \
	static const struct xen_rgpio_config xen_rgpio_config_##n = {                              \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)},                   \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
	};                                                                                         \
	static struct xen_rgpio_data xen_rgpio_data_##n;                                           \
	DEVICE_DT_INST_DEFINE(n, xen_rgpio_init, NULL, &xen_rgpio_data_##n,                        \
			      &xen_rgpio_config_##n, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,       \
			      &xen_rgpio_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_RGPIO_INIT)
