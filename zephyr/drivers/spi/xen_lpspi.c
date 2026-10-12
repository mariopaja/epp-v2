/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP LPSPI controller driver for a Zephyr Xen DomU (xenvm) that
 * gets the LPSPI passed through by Xen. Pin mux and clock are set up outside
 * the DomU, so unlike the SoC driver this one needs no SCMI, no pinctrl and
 * no MCUX headers. Controller mode, 8-bit words, full duplex, polled (no
 * interrupts, no DMA), chip select by GPIO.
 */

#define DT_DRV_COMPAT epp_xen_lpspi

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(epp_xen_lpspi, CONFIG_SPI_LOG_LEVEL);

/* LPSPI registers */
#define LPSPI_VERID 0x00
#define LPSPI_PARAM 0x04
#define LPSPI_CR    0x10
#define LPSPI_SR    0x14
#define LPSPI_CFGR1 0x24
#define LPSPI_CCR   0x40
#define LPSPI_FSR   0x5c
#define LPSPI_TCR   0x60
#define LPSPI_TDR   0x64
#define LPSPI_RSR   0x70
#define LPSPI_RDR   0x74

#define CR_MEN BIT(0)
#define CR_RST BIT(1)
#define CR_RTF BIT(8)
#define CR_RRF BIT(9)

#define SR_W1C GENMASK(13, 8)

#define CFGR1_MASTER BIT(0)

#define TCR_FRAMESZ(bits) ((bits) - 1U)
#define TCR_LSBF          BIT(23)
#define TCR_PRESCALE(p)   ((p) << 27)
#define TCR_CPHA          BIT(30)
#define TCR_CPOL          BIT(31)

#define RSR_RXEMPTY BIT(1)

#define TIMEOUT_MS 100

struct xen_lpspi_config {
	DEVICE_MMIO_NAMED_ROM(reg_base);
	uint32_t fclk;
};

struct xen_lpspi_data {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_mutex lock;
	uint32_t tx_fifo;
	/* Configuration of the last transfer, to skip reprogramming */
	const struct spi_config *last;
	uint32_t tcr;
};

/* Used by the DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(dev)  ((const struct xen_lpspi_config *)(dev)->config)
#define DEV_DATA(dev) ((struct xen_lpspi_data *)(dev)->data)

static inline uint32_t rd(const struct device *dev, uint32_t off)
{
	return sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static inline void wr(const struct device *dev, uint32_t off, uint32_t val)
{
	sys_write32(val, DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

/* SCK = fclk / (2^prescale * (sckdiv + 2)), the highest rate <= freq */
static void sck_divider(uint32_t fclk, uint32_t freq, uint32_t *prescale, uint32_t *sckdiv)
{
	uint32_t div = DIV_ROUND_UP(fclk, MAX(freq, 1U));

	for (uint32_t p = 0U; p < 8U; p++) {
		uint32_t d = DIV_ROUND_UP(div, BIT(p));

		if (d <= 257U) {
			*prescale = p;
			*sckdiv = (d > 2U) ? (d - 2U) : 0U;
			return;
		}
	}
	*prescale = 7U;
	*sckdiv = 255U;
}

static int xen_lpspi_configure(const struct device *dev, const struct spi_config *config)
{
	const struct xen_lpspi_config *cfg = dev->config;
	struct xen_lpspi_data *data = dev->data;
	uint32_t prescale, sckdiv;

	if (data->last == config) {
		return 0;
	}
	if ((SPI_OP_MODE_GET(config->operation) != SPI_OP_MODE_CONTROLLER) ||
	    (SPI_WORD_SIZE_GET(config->operation) != 8U) ||
	    ((config->operation & SPI_LINES_MASK) != SPI_LINES_SINGLE) ||
	    ((config->operation & SPI_MODE_LOOP) != 0U)) {
		LOG_ERR("only controller mode, 8-bit words, single line");
		return -ENOTSUP;
	}

	if (spi_cs_is_gpio(config)) {
		int ret = gpio_pin_configure_dt(&config->cs.gpio, GPIO_OUTPUT_INACTIVE);

		if (ret != 0) {
			return ret;
		}
	}

	sck_divider(cfg->fclk, config->frequency, &prescale, &sckdiv);

	/* CFGR1 and CCR can only be written while the module is disabled */
	wr(dev, LPSPI_CR, 0U);
	wr(dev, LPSPI_CFGR1, CFGR1_MASTER);
	/* SCK divider; delays (DBT, PCSSCK, SCKPCS) half an SCK period */
	wr(dev, LPSPI_CCR, sckdiv | ((sckdiv / 2U) << 8) | ((sckdiv / 2U) << 16) |
				   ((sckdiv / 2U) << 24));
	wr(dev, LPSPI_CR, CR_MEN | CR_RTF | CR_RRF);
	wr(dev, LPSPI_SR, SR_W1C);

	data->tcr = TCR_FRAMESZ(8U) | TCR_PRESCALE(prescale) |
		    (((config->operation & SPI_MODE_CPOL) != 0U) ? TCR_CPOL : 0U) |
		    (((config->operation & SPI_MODE_CPHA) != 0U) ? TCR_CPHA : 0U) |
		    (((config->operation & SPI_TRANSFER_LSB) != 0U) ? TCR_LSBF : 0U);
	data->last = config;

	LOG_DBG("%u Hz requested, %u Hz (prescale %u, sckdiv %u)", config->frequency,
		cfg->fclk / ((1U << prescale) * (sckdiv + 2U)), prescale, sckdiv);
	return 0;
}

/* Send one byte and receive one, full duplex */
static int xfer_byte(const struct device *dev, uint8_t tx, uint8_t *rx)
{
	struct xen_lpspi_data *data = dev->data;
	int64_t end = k_uptime_get() + TIMEOUT_MS;
	uint32_t rdr;

	while ((rd(dev, LPSPI_FSR) & 0x1fU) >= data->tx_fifo) {
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
	}
	wr(dev, LPSPI_TDR, tx);

	while ((rd(dev, LPSPI_RSR) & RSR_RXEMPTY) != 0U) {
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
	}
	rdr = rd(dev, LPSPI_RDR);
	if (rx != NULL) {
		*rx = rdr & 0xffU;
	}
	return 0;
}

/* Byte @p i of a buffer set, or NULL past its end / for a NULL buffer */
static uint8_t *buf_at(const struct spi_buf_set *set, size_t i)
{
	if (set == NULL) {
		return NULL;
	}
	for (size_t b = 0U; b < set->count; b++) {
		if (i < set->buffers[b].len) {
			return (set->buffers[b].buf != NULL) ? &((uint8_t *)set->buffers[b].buf)[i]
							      : NULL;
		}
		i -= set->buffers[b].len;
	}
	return NULL;
}

static size_t set_len(const struct spi_buf_set *set)
{
	size_t len = 0U;

	for (size_t b = 0U; (set != NULL) && (b < set->count); b++) {
		len += set->buffers[b].len;
	}
	return len;
}

/* GPIO chip select, with the configured delay inside the active phase */
static void cs_set(const struct spi_config *config, bool active)
{
	if (!spi_cs_is_gpio(config)) {
		return;
	}
	if (active) {
		gpio_pin_set_dt(&config->cs.gpio, 1);
		k_busy_wait(config->cs.delay);
	} else {
		k_busy_wait(config->cs.delay);
		gpio_pin_set_dt(&config->cs.gpio, 0);
	}
}

static int xen_lpspi_transceive(const struct device *dev, const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	struct xen_lpspi_data *data = dev->data;
	size_t len = MAX(set_len(tx_bufs), set_len(rx_bufs));
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = xen_lpspi_configure(dev, config);
	if (ret == 0) {
		/* Drop data of an earlier aborted transfer, new frame format */
		wr(dev, LPSPI_CR, CR_MEN | CR_RTF | CR_RRF);
		wr(dev, LPSPI_SR, SR_W1C);
		wr(dev, LPSPI_TCR, data->tcr);

		cs_set(config, true);
		for (size_t i = 0U; (i < len) && (ret == 0); i++) {
			uint8_t *tx = buf_at(tx_bufs, i);

			ret = xfer_byte(dev, (tx != NULL) ? *tx : 0x00U, buf_at(rx_bufs, i));
		}
		if ((ret != 0) || ((config->operation & SPI_HOLD_ON_CS) == 0U)) {
			cs_set(config, false);
		}
	}

	k_mutex_unlock(&data->lock);
	return ret;
}

static int xen_lpspi_release(const struct device *dev, const struct spi_config *config)
{
	ARG_UNUSED(dev);

	cs_set(config, false);
	return 0;
}

static int xen_lpspi_init(const struct device *dev)
{
	struct xen_lpspi_data *data = dev->data;
	uint32_t param;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);
	k_mutex_init(&data->lock);

	/* Reset the controller, whatever Dom0 or U-Boot left */
	wr(dev, LPSPI_CR, CR_RST);
	wr(dev, LPSPI_CR, 0U);

	param = rd(dev, LPSPI_PARAM);
	data->tx_fifo = BIT(param & 0xffU);
	LOG_INF("LPSPI at 0x%lx: VERID 0x%08x PARAM 0x%08x (TX FIFO %u)",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), rd(dev, LPSPI_VERID), param,
		data->tx_fifo);
	return 0;
}

static DEVICE_API(spi, xen_lpspi_api) = {
	.transceive = xen_lpspi_transceive,
	.release = xen_lpspi_release,
};

#define XEN_LPSPI_INIT(n)                                                                          \
	static const struct xen_lpspi_config xen_lpspi_config_##n = {                              \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
		.fclk = DT_INST_PROP(n, functional_clock_frequency),                               \
	};                                                                                         \
	static struct xen_lpspi_data xen_lpspi_data_##n;                                           \
	SPI_DEVICE_DT_INST_DEFINE(n, xen_lpspi_init, NULL, &xen_lpspi_data_##n,                    \
				  &xen_lpspi_config_##n, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,    \
				  &xen_lpspi_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_LPSPI_INIT)
