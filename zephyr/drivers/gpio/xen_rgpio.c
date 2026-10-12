/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP RGPIO driver for a Zephyr Xen DomU (xenvm) that gets the GPIO
 * controller passed through by Xen. Pin mux and clocks are set up outside
 * the DomU, so unlike the SoC driver this one needs no SCMI, no pinctrl and
 * no MCUX headers. Inputs and outputs; pin interrupts (edges and levels) when
 * the node has an interrupt (Xen routes the GPIO interrupt to the DomU).
 */

#define DT_DRV_COMPAT epp_xen_rgpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
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
/* Interrupt control per pin and interrupt status (Linux: port base 0x80) */
#define RGPIO_ICR(n) (0x80 + 4 * (n))
#define RGPIO_ISFR   0x120

#define ICR_IRQC_SHIFT 16
#define ICR_IRQC_MASK  (0xfU << ICR_IRQC_SHIFT)
#define IRQC_DISABLED  0x0U
#define IRQC_LOW       0x8U
#define IRQC_RISING    0x9U
#define IRQC_FALLING   0xaU
#define IRQC_BOTH      0xbU
#define IRQC_HIGH      0xcU

/*
 * The GPIO API needs gpio_driver_config / gpio_driver_data first, so the
 * MMIO region is a named one (the unnamed DEVICE_MMIO_ROM/RAM must be first).
 */
struct xen_rgpio_config {
	struct gpio_driver_config common;
	DEVICE_MMIO_NAMED_ROM(reg_base);
	/* Connects and enables the interrupt; NULL without one */
	void (*irq_config)(const struct device *dev);
};

struct xen_rgpio_data {
	struct gpio_driver_data common;
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_spinlock lock;
	sys_slist_t callbacks;
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

static int xen_rgpio_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					     enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct xen_rgpio_config *cfg = dev->config;
	struct xen_rgpio_data *data = dev->data;
	uint32_t irqc;
	k_spinlock_key_t key;

	if (cfg->irq_config == NULL) {
		return -ENOTSUP; /* no interrupt routed to this DomU */
	}

	if (mode == GPIO_INT_MODE_DISABLED) {
		irqc = IRQC_DISABLED;
	} else if (mode == GPIO_INT_MODE_LEVEL) {
		if (trig == GPIO_INT_TRIG_LOW) {
			irqc = IRQC_LOW;
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			irqc = IRQC_HIGH;
		} else {
			return -EINVAL;
		}
	} else {
		irqc = (trig == GPIO_INT_TRIG_LOW)    ? IRQC_FALLING
		       : (trig == GPIO_INT_TRIG_HIGH) ? IRQC_RISING
						      : IRQC_BOTH;
	}

	key = k_spin_lock(&data->lock);
	/* Clear a stale flag, then set the trigger */
	wr(dev, RGPIO_ISFR, BIT(pin));
	wr(dev, RGPIO_ICR(pin), (rd(dev, RGPIO_ICR(pin)) & ~ICR_IRQC_MASK) |
					(irqc << ICR_IRQC_SHIFT));
	k_spin_unlock(&data->lock, key);

	return 0;
}

static int xen_rgpio_manage_callback(const struct device *dev, struct gpio_callback *callback,
				     bool set)
{
	struct xen_rgpio_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static uint32_t xen_rgpio_get_pending_int(const struct device *dev)
{
	return rd(dev, RGPIO_ISFR);
}

/* Clear the flags first: a level interrupt still active sets them again */
static void xen_rgpio_isr(const struct device *dev)
{
	struct xen_rgpio_data *data = dev->data;
	uint32_t pending = rd(dev, RGPIO_ISFR);

	wr(dev, RGPIO_ISFR, pending);
	gpio_fire_callbacks(&data->callbacks, dev, pending);
}

static int xen_rgpio_init(const struct device *dev)
{
	const struct xen_rgpio_config *cfg = dev->config;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);

	/* Not PCNS/PCNP: secure-only, reading them from the DomU aborts */
	LOG_INF("RGPIO at 0x%lx: VERID 0x%08x PARAM 0x%08x PDDR 0x%08x PDOR 0x%08x PDIR 0x%08x",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), rd(dev, RGPIO_VERID),
		rd(dev, RGPIO_PARAM), rd(dev, RGPIO_PDDR), rd(dev, RGPIO_PDOR), rd(dev, RGPIO_PDIR));
	if (cfg->irq_config != NULL) {
		wr(dev, RGPIO_ISFR, UINT32_MAX);
		cfg->irq_config(dev);
	}
	return 0;
}

static DEVICE_API(gpio, xen_rgpio_api) = {
	.pin_configure = xen_rgpio_pin_configure,
	.port_get_raw = xen_rgpio_port_get_raw,
	.port_set_masked_raw = xen_rgpio_port_set_masked_raw,
	.port_set_bits_raw = xen_rgpio_port_set_bits_raw,
	.port_clear_bits_raw = xen_rgpio_port_clear_bits_raw,
	.port_toggle_bits = xen_rgpio_port_toggle_bits,
	.pin_interrupt_configure = xen_rgpio_pin_interrupt_configure,
	.manage_callback = xen_rgpio_manage_callback,
	.get_pending_int = xen_rgpio_get_pending_int,
};

/* The interrupt, if the node has one (routed by Xen) */
#define XEN_RGPIO_IRQ_CONFIG(n)                                                                    \
	static void xen_rgpio_irq_config_##n(const struct device *dev)                             \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), xen_rgpio_isr,              \
			    DEVICE_DT_INST_GET(n), DT_INST_IRQ(n, flags));                         \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}

#define XEN_RGPIO_INIT(n)                                                                          \
	IF_ENABLED(DT_INST_IRQ_HAS_IDX(n, 0), (XEN_RGPIO_IRQ_CONFIG(n)))                         \
	static const struct xen_rgpio_config xen_rgpio_config_##n = {                              \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n)},                   \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
		.irq_config = COND_CODE_1(DT_INST_IRQ_HAS_IDX(n, 0),                               \
					  (xen_rgpio_irq_config_##n), (NULL)),                     \
	};                                                                                         \
	static struct xen_rgpio_data xen_rgpio_data_##n;                                           \
	DEVICE_DT_INST_DEFINE(n, xen_rgpio_init, NULL, &xen_rgpio_data_##n,                        \
			      &xen_rgpio_config_##n, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,       \
			      &xen_rgpio_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_RGPIO_INIT)
