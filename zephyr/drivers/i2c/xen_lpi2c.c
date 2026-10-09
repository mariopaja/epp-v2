/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP LPI2C controller driver for a Zephyr Xen DomU (xenvm) that
 * gets the LPI2C passed through by Xen. Pin mux and clock are set up outside
 * the DomU, so unlike the SoC driver this one needs no SCMI, no pinctrl and
 * no MCUX headers. 7-bit addresses, standard and fast mode, polled (no
 * interrupts, no DMA).
 */

#define DT_DRV_COMPAT epp_xen_lpi2c

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/dt-bindings/i2c/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(epp_xen_lpi2c, CONFIG_I2C_LOG_LEVEL);

/* LPI2C controller registers */
#define LPI2C_VERID  0x00
#define LPI2C_PARAM  0x04
#define LPI2C_MCR    0x10
#define LPI2C_MSR    0x14
#define LPI2C_MCFGR1 0x24
#define LPI2C_MCCR0  0x48
#define LPI2C_MFSR   0x5c
#define LPI2C_MTDR   0x60
#define LPI2C_MRDR   0x70

#define MCR_MEN BIT(0)
#define MCR_RST BIT(1)
#define MCR_RTF BIT(8)
#define MCR_RRF BIT(9)

#define MSR_SDF    BIT(9)
#define MSR_NDF    BIT(10)
#define MSR_ALF    BIT(11)
#define MSR_FEF    BIT(12)
#define MSR_PLTF   BIT(13)
#define MSR_MBF    BIT(24)
#define MSR_W1C    GENMASK(14, 8)
#define MSR_ERRORS (MSR_NDF | MSR_ALF | MSR_FEF | MSR_PLTF)

/* Command in MTDR[10:8] */
#define CMD_TX    (0U << 8)
#define CMD_RX    (1U << 8)
#define CMD_STOP  (2U << 8)
#define CMD_START (4U << 8)

#define MRDR_RXEMPTY BIT(14)

#define MCCR0(clklo, clkhi, sethold, datavd)                                                       \
	((clklo) | ((clkhi) << 8) | ((sethold) << 16) | ((datavd) << 24))

#define TIMEOUT_MS 100

struct xen_lpi2c_config {
	DEVICE_MMIO_NAMED_ROM(reg_base);
	uint32_t fclk;
	uint32_t bitrate;
};

struct xen_lpi2c_data {
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_mutex lock;
	uint32_t dev_config;
	uint32_t tx_fifo;
};

/* Used by the DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(dev)  ((const struct xen_lpi2c_config *)(dev)->config)
#define DEV_DATA(dev) ((struct xen_lpi2c_data *)(dev)->data)

static inline uint32_t rd(const struct device *dev, uint32_t off)
{
	return sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static inline void wr(const struct device *dev, uint32_t off, uint32_t val)
{
	sys_write32(val, DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static int check_errors(const struct device *dev)
{
	uint32_t msr = rd(dev, LPI2C_MSR);

	if ((msr & MSR_NDF) != 0U) {
		return -ENXIO; /* NACK: no device at this address, or data refused */
	}
	if ((msr & MSR_ERRORS) != 0U) {
		LOG_DBG("bus error, MSR 0x%08x", msr);
		return -EIO;
	}
	return 0;
}

static int push_cmd(const struct device *dev, uint32_t cmd)
{
	struct xen_lpi2c_data *data = dev->data;
	int64_t end = k_uptime_get() + TIMEOUT_MS;

	while ((rd(dev, LPI2C_MFSR) & 0xffU) >= data->tx_fifo) {
		int ret = check_errors(dev);

		if (ret != 0) {
			return ret;
		}
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
	}
	wr(dev, LPI2C_MTDR, cmd);
	return check_errors(dev);
}

static int pop_byte(const struct device *dev, uint8_t *byte)
{
	int64_t end = k_uptime_get() + TIMEOUT_MS;

	while (true) {
		uint32_t mrdr = rd(dev, LPI2C_MRDR);
		int ret;

		if ((mrdr & MRDR_RXEMPTY) == 0U) {
			*byte = mrdr & 0xffU;
			return 0;
		}
		ret = check_errors(dev);
		if (ret != 0) {
			return ret;
		}
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
	}
}

static int wait_stop(const struct device *dev)
{
	int64_t end = k_uptime_get() + TIMEOUT_MS;

	int ret;

	while ((rd(dev, LPI2C_MSR) & MSR_SDF) == 0U) {
		ret = check_errors(dev);
		if (ret != 0) {
			return ret;
		}
		if (k_uptime_get() > end) {
			return -ETIMEDOUT;
		}
	}
	/* A NACK ends the transfer with a stop as well */
	ret = check_errors(dev);
	wr(dev, LPI2C_MSR, MSR_SDF);
	return ret;
}

/* After an error: drop queued commands and data, end the transfer */
static void recover(const struct device *dev)
{
	int64_t end = k_uptime_get() + TIMEOUT_MS;

	wr(dev, LPI2C_MCR, rd(dev, LPI2C_MCR) | MCR_RTF | MCR_RRF);
	wr(dev, LPI2C_MSR, MSR_W1C);
	if ((rd(dev, LPI2C_MSR) & MSR_MBF) != 0U) {
		wr(dev, LPI2C_MTDR, CMD_STOP);
	}
	while (((rd(dev, LPI2C_MSR) & MSR_MBF) != 0U) && (k_uptime_get() <= end)) {
	}
	wr(dev, LPI2C_MSR, MSR_W1C);
}

static int xen_lpi2c_configure(const struct device *dev, uint32_t dev_config)
{
	const struct xen_lpi2c_config *cfg = dev->config;
	struct xen_lpi2c_data *data = dev->data;
	uint32_t prescale;
	uint32_t mccr0;

	if (((dev_config & I2C_MODE_CONTROLLER) == 0U) || ((dev_config & I2C_ADDR_10_BITS) != 0U)) {
		return -ENOTSUP;
	}

	/*
	 * SCL period = (CLKLO + CLKHI + 2 + latency) * 2^PRESCALE / fclk, for
	 * fclk = 24 MHz: standard mode 6 MHz ticks (low 5 us, high 5 us), fast
	 * mode 24 MHz ticks (low 1.4 us, high ~1.1 us). Other fclk values scale
	 * the bit rate accordingly.
	 */
	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		prescale = 2U;
		mccr0 = MCCR0(29U, 28U, 23U, 5U);
		break;
	case I2C_SPEED_FAST:
		prescale = 0U;
		mccr0 = MCCR0(33U, 24U, 15U, 7U);
		break;
	default:
		return -ENOTSUP;
	}
	if (cfg->fclk != 24000000U) {
		LOG_WRN("functional clock %u Hz, timing assumes 24 MHz", cfg->fclk);
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	wr(dev, LPI2C_MCR, 0U);
	wr(dev, LPI2C_MCFGR1, prescale);
	wr(dev, LPI2C_MCCR0, mccr0);
	wr(dev, LPI2C_MCR, MCR_MEN);
	data->dev_config = dev_config;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int xen_lpi2c_get_config(const struct device *dev, uint32_t *dev_config)
{
	struct xen_lpi2c_data *data = dev->data;

	*dev_config = data->dev_config;
	return 0;
}

static int xen_lpi2c_transfer(const struct device *dev, struct i2c_msg *msgs, uint8_t num_msgs,
			      uint16_t addr)
{
	struct xen_lpi2c_data *data = dev->data;
	bool prev_read = false;
	int ret = 0;

	if (addr > 0x7fU) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	wr(dev, LPI2C_MCR, rd(dev, LPI2C_MCR) | MCR_RTF | MCR_RRF);
	wr(dev, LPI2C_MSR, MSR_W1C);

	for (uint8_t i = 0U; (i < num_msgs) && (ret == 0); i++) {
		struct i2c_msg *msg = &msgs[i];
		bool read = (msg->flags & I2C_MSG_READ) != 0U;

		if ((msg->flags & I2C_MSG_ADDR_10_BITS) != 0U) {
			ret = -ENOTSUP;
			break;
		}

		/* (Repeated) start with the address on the first message, on
		 * request and when the direction changes
		 */
		if ((i == 0U) || ((msg->flags & I2C_MSG_RESTART) != 0U) || (read != prev_read)) {
			ret = push_cmd(dev, CMD_START | (addr << 1) | (read ? 1U : 0U));
		}

		if (read) {
			for (uint32_t done = 0U; (done < msg->len) && (ret == 0);) {
				uint32_t n = MIN(msg->len - done, 256U);

				ret = push_cmd(dev, CMD_RX | (n - 1U));
				for (uint32_t j = 0U; (j < n) && (ret == 0); j++) {
					ret = pop_byte(dev, &msg->buf[done + j]);
				}
				done += n;
			}
		} else {
			for (uint32_t j = 0U; (j < msg->len) && (ret == 0); j++) {
				ret = push_cmd(dev, CMD_TX | msg->buf[j]);
			}
		}

		if ((ret == 0) && ((msg->flags & I2C_MSG_STOP) != 0U)) {
			ret = push_cmd(dev, CMD_STOP);
			if (ret == 0) {
				ret = wait_stop(dev);
			}
		}
		prev_read = read;
	}

	if (ret != 0) {
		recover(dev);
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

static int xen_lpi2c_init(const struct device *dev)
{
	const struct xen_lpi2c_config *cfg = dev->config;
	struct xen_lpi2c_data *data = dev->data;
	uint32_t param;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);
	k_mutex_init(&data->lock);

	param = rd(dev, LPI2C_PARAM);
	data->tx_fifo = BIT(param & 0xfU);
	LOG_INF("LPI2C at 0x%lx: VERID 0x%08x PARAM 0x%08x (TX FIFO %u)",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), rd(dev, LPI2C_VERID), param,
		data->tx_fifo);

	/* Reset the controller, whatever Dom0 or U-Boot left */
	wr(dev, LPI2C_MCR, MCR_RST);
	wr(dev, LPI2C_MCR, 0U);

	return xen_lpi2c_configure(dev, I2C_MODE_CONTROLLER |
					(cfg->bitrate >= I2C_BITRATE_FAST ? I2C_SPEED_SET(I2C_SPEED_FAST)
									   : I2C_SPEED_SET(I2C_SPEED_STANDARD)));
}

static DEVICE_API(i2c, xen_lpi2c_api) = {
	.configure = xen_lpi2c_configure,
	.get_config = xen_lpi2c_get_config,
	.transfer = xen_lpi2c_transfer,
};

#define XEN_LPI2C_INIT(n)                                                                          \
	static const struct xen_lpi2c_config xen_lpi2c_config_##n = {                              \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
		.fclk = DT_INST_PROP(n, functional_clock_frequency),                               \
		.bitrate = DT_INST_PROP_OR(n, clock_frequency, I2C_BITRATE_STANDARD),              \
	};                                                                                         \
	static struct xen_lpi2c_data xen_lpi2c_data_##n;                                           \
	I2C_DEVICE_DT_INST_DEFINE(n, xen_lpi2c_init, NULL, &xen_lpi2c_data_##n,                    \
				  &xen_lpi2c_config_##n, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,    \
				  &xen_lpi2c_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_LPI2C_INIT)
