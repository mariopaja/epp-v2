/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP LPUART driver for a Zephyr Xen DomU (xenvm) that gets the
 * LPUART passed through by Xen. Pin mux and clock are set up outside the
 * DomU, so unlike the SoC driver this one needs no SCMI, no pinctrl and no
 * MCUX headers. Polled (poll_in/poll_out), 8 data bits, no parity, 1 stop
 * bit; baud rate from the devicetree.
 */

#define DT_DRV_COMPAT epp_xen_lpuart

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(epp_xen_lpuart, CONFIG_UART_LOG_LEVEL);

/* LPUART registers */
#define LPUART_VERID  0x00
#define LPUART_GLOBAL 0x08
#define LPUART_BAUD   0x10
#define LPUART_STAT   0x14
#define LPUART_CTRL   0x18
#define LPUART_DATA   0x1c

#define GLOBAL_RST BIT(1)

#define BAUD_OSR(x)   ((uint32_t)(x) << 24) /* oversampling - 1 */
#define BAUD_BOTHEDGE BIT(17)
#define BAUD_SBR_MAX  0x1fffU

#define STAT_TDRE BIT(23)
#define STAT_RDRF BIT(21)
#define STAT_OR   BIT(19)
#define STAT_NF   BIT(18)
#define STAT_FE   BIT(17)
#define STAT_PF   BIT(16)
#define STAT_ERRS (STAT_OR | STAT_NF | STAT_FE | STAT_PF)

#define CTRL_TE    BIT(19)
#define CTRL_RE    BIT(18)
#define CTRL_LOOPS BIT(7) /* with RSRC = 0: TX looped back to RX internally */

struct xen_lpuart_config {
	DEVICE_MMIO_NAMED_ROM(reg_base);
	uint32_t fclk;
	uint32_t baud;
	bool internal_loopback;
};

struct xen_lpuart_data {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_spinlock lock;
};

/* Used by the DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(dev)  ((const struct xen_lpuart_config *)(dev)->config)
#define DEV_DATA(dev) ((struct xen_lpuart_data *)(dev)->data)

static inline uint32_t rd(const struct device *dev, uint32_t off)
{
	return sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static inline void wr(const struct device *dev, uint32_t off, uint32_t val)
{
	sys_write32(val, DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static int xen_lpuart_poll_in(const struct device *dev, unsigned char *c)
{
	if ((rd(dev, LPUART_STAT) & STAT_RDRF) == 0U) {
		return -1;
	}
	*c = rd(dev, LPUART_DATA) & 0xffU;
	return 0;
}

static void xen_lpuart_poll_out(const struct device *dev, unsigned char c)
{
	struct xen_lpuart_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	while ((rd(dev, LPUART_STAT) & STAT_TDRE) == 0U) {
	}
	wr(dev, LPUART_DATA, c);
	k_spin_unlock(&data->lock, key);
}

/* Errors since the last call; the flags are write-1-to-clear */
static int xen_lpuart_err_check(const struct device *dev)
{
	uint32_t stat = rd(dev, LPUART_STAT) & STAT_ERRS;
	int err = 0;

	wr(dev, LPUART_STAT, stat);
	err |= ((stat & STAT_OR) != 0U) ? UART_ERROR_OVERRUN : 0;
	err |= ((stat & STAT_NF) != 0U) ? UART_ERROR_NOISE : 0;
	err |= ((stat & STAT_FE) != 0U) ? UART_ERROR_FRAMING : 0;
	err |= ((stat & STAT_PF) != 0U) ? UART_ERROR_PARITY : 0;
	return err;
}

/* baud = fclk / (oversampling x SBR): the oversampling (4..32) and SBR with
 * the smallest error
 */
static uint32_t baud_reg(uint32_t fclk, uint32_t baud, uint32_t *actual)
{
	uint32_t best = 0U, best_err = UINT32_MAX;

	for (uint32_t osr = 4U; osr <= 32U; osr++) {
		uint32_t sbr = CLAMP((fclk + (osr * baud) / 2U) / (osr * baud), 1U, BAUD_SBR_MAX);
		uint32_t got = fclk / (osr * sbr);
		uint32_t err = (got > baud) ? (got - baud) : (baud - got);

		if (err < best_err) {
			best_err = err;
			*actual = got;
			best = BAUD_OSR(osr - 1U) | sbr | ((osr < 8U) ? BAUD_BOTHEDGE : 0U);
		}
	}
	return best;
}

static int xen_lpuart_init(const struct device *dev)
{
	const struct xen_lpuart_config *cfg = dev->config;
	uint32_t actual = 0U;
	uint32_t baud;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);

	/* Reset, whatever Dom0 or U-Boot left; 8N1 is the reset default */
	wr(dev, LPUART_GLOBAL, GLOBAL_RST);
	wr(dev, LPUART_GLOBAL, 0U);
	baud = baud_reg(cfg->fclk, cfg->baud, &actual);
	wr(dev, LPUART_BAUD, baud);
	wr(dev, LPUART_STAT, STAT_ERRS);
	wr(dev, LPUART_CTRL, CTRL_TE | CTRL_RE | (cfg->internal_loopback ? CTRL_LOOPS : 0U));

	LOG_INF("LPUART at 0x%lx: VERID 0x%08x, %u baud (%u actual)%s",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), rd(dev, LPUART_VERID),
		cfg->baud, actual, cfg->internal_loopback ? ", internal loopback" : "");
	return 0;
}

static DEVICE_API(uart, xen_lpuart_api) = {
	.poll_in = xen_lpuart_poll_in,
	.poll_out = xen_lpuart_poll_out,
	.err_check = xen_lpuart_err_check,
};

#define XEN_LPUART_INIT(n)                                                                         \
	static const struct xen_lpuart_config xen_lpuart_config_##n = {                            \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
		.fclk = DT_INST_PROP(n, functional_clock_frequency),                               \
		.baud = DT_INST_PROP(n, current_speed),                                            \
		.internal_loopback = DT_INST_PROP(n, internal_loopback),                           \
	};                                                                                         \
	static struct xen_lpuart_data xen_lpuart_data_##n;                                         \
	DEVICE_DT_INST_DEFINE(n, xen_lpuart_init, NULL, &xen_lpuart_data_##n,                      \
			      &xen_lpuart_config_##n, PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY,   \
			      &xen_lpuart_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_LPUART_INIT)
