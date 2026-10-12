/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Minimal NXP FlexCAN driver for a Zephyr Xen DomU (xenvm) that gets the
 * FlexCAN passed through by Xen, with its interrupt. Pin mux and clock are
 * set up outside the DomU, so unlike the SoC driver this one needs no SCMI,
 * no pinctrl and no MCUX headers. Classic CAN (no CAN FD), one mailbox per
 * receive filter, one transmit mailbox. Start-up sequence and i.MX95 quirks
 * (ECC RAM init, MECR, reserved mailbox 0) as in Linux's flexcan driver.
 */

#define DT_DRV_COMPAT epp_xen_flexcan

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(epp_xen_flexcan, CONFIG_CAN_LOG_LEVEL);

/* Registers */
#define FC_MCR      0x00
#define FC_CTRL1    0x04
#define FC_TIMER    0x08
#define FC_RXMGMASK 0x10
#define FC_RX14MASK 0x14
#define FC_RX15MASK 0x18
#define FC_ECR      0x1c
#define FC_ESR1     0x20
#define FC_IMASK1   0x28
#define FC_IFLAG1   0x30
#define FC_CTRL2    0x34
#define FC_RXFGMASK 0x48
#define FC_MB(n)    (0x80 + 16 * (n)) /* 8-byte payload mailboxes */
#define FC_RXIMR(n) (0x880 + 4 * (n))
#define FC_MECR     0xae0
/* RAM to initialize for ECC (CTRL2[WRMFRZ]): mailboxes up to RX SMBs */
#define FC_RAM_START 0x80
#define FC_RAM_END   0xae0

#define MCR_MDIS    BIT(31)
#define MCR_FRZ     BIT(30)
#define MCR_HALT    BIT(28)
#define MCR_NOTRDY  BIT(27)
#define MCR_SOFTRST BIT(25)
#define MCR_FRZACK  BIT(24)
#define MCR_SUPV    BIT(23)
#define MCR_WRNEN   BIT(21)
#define MCR_LPMACK  BIT(20)
#define MCR_SRXDIS  BIT(17)
#define MCR_IRMQ    BIT(16)
#define MCR_IDAM_C  (0x2U << 8)
#define MCR_MAXMB(n) ((n) & 0x7fU)

#define CTRL1_PRESDIV(x) ((uint32_t)(x) << 24)
#define CTRL1_RJW(x)     ((uint32_t)(x) << 22)
#define CTRL1_PSEG1(x)   ((uint32_t)(x) << 19)
#define CTRL1_PSEG2(x)   ((uint32_t)(x) << 16)
#define CTRL1_BOFFMSK    BIT(15)
#define CTRL1_LPB        BIT(12)
#define CTRL1_TWRNMSK    BIT(11)
#define CTRL1_RWRNMSK    BIT(10)
#define CTRL1_LBUF       BIT(4)
#define CTRL1_LOM        BIT(3)
#define CTRL1_PROPSEG(x) ((uint32_t)(x) << 0)

#define CTRL2_ECRWRE BIT(29)
#define CTRL2_WRMFRZ BIT(28)
#define CTRL2_RRS    BIT(17)

#define MECR_ECRWRDIS   BIT(31)
#define MECR_HANCEI_MSK BIT(19)
#define MECR_FANCEI_MSK BIT(18)
#define MECR_NCEFAFRZ   BIT(7)

#define ESR1_BOFFDONEINT BIT(19)
#define ESR1_TWRNINT     BIT(17)
#define ESR1_RWRNINT     BIT(16)
#define ESR1_FLTCONF(r)  (((r) >> 4) & 0x3U)
#define ESR1_BOFFINT     BIT(2)
#define ESR1_ERRINT      BIT(1)
#define ESR1_STATE_INTS  (ESR1_BOFFDONEINT | ESR1_TWRNINT | ESR1_RWRNINT | ESR1_BOFFINT)

/* Mailbox control/status word */
#define CS_CODE(x)     ((uint32_t)(x) << 24)
#define CS_CODE_GET(r) (((r) >> 24) & 0xfU)
#define CS_SRR         BIT(22)
#define CS_IDE         BIT(21)
#define CS_RTR         BIT(20)
#define CS_DLC(x)      ((uint32_t)(x) << 16)
#define CS_DLC_GET(r)  (((r) >> 16) & 0xfU)

#define CODE_RX_INACTIVE 0x0U
#define CODE_RX_EMPTY    0x4U
#define CODE_TX_INACTIVE 0x8U
#define CODE_TX_DATA     0xcU

#define ID_STD_SHIFT 18

/* Mailboxes: 0 reserved (TX inactive, erratum ERR005829), filters, TX */
#define MB_RESERVED  0
#define MB_RX_FIRST  1
#define NUM_FILTERS  8
#define MB_TX        (MB_RX_FIRST + NUM_FILTERS)
#define MB_LAST      MB_TX

#define TIMEOUT_US 100000

struct xen_flexcan_config {
	const struct can_driver_config common;
	DEVICE_MMIO_NAMED_ROM(reg_base);
	uint32_t fclk;
	void (*irq_config)(const struct device *dev);
};

struct xen_flexcan_filter {
	can_rx_callback_t callback;
	void *user_data;
	struct can_filter filter;
};

struct xen_flexcan_data {
	struct can_driver_data common;
	DEVICE_MMIO_NAMED_RAM(reg_base);
	struct k_mutex lock;
	struct k_spinlock isr_lock;
	struct k_sem tx_idle;
	can_tx_callback_t tx_callback;
	void *tx_user_data;
	struct xen_flexcan_filter filters[NUM_FILTERS];
	struct can_timing timing;
	enum can_state state;
};

/* Used by the DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(dev)  ((const struct xen_flexcan_config *)(dev)->config)
#define DEV_DATA(dev) ((struct xen_flexcan_data *)(dev)->data)

static inline uint32_t rd(const struct device *dev, uint32_t off)
{
	return sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static inline void wr(const struct device *dev, uint32_t off, uint32_t val)
{
	sys_write32(val, DEVICE_MMIO_NAMED_GET(dev, reg_base) + off);
}

static int wait_mcr(const struct device *dev, uint32_t mask, uint32_t value)
{
	for (int i = 0; i < TIMEOUT_US / 10; i++) {
		if ((rd(dev, FC_MCR) & mask) == value) {
			return 0;
		}
		k_busy_wait(10);
	}
	return -ETIMEDOUT;
}

static int freeze(const struct device *dev)
{
	wr(dev, FC_MCR, rd(dev, FC_MCR) | MCR_FRZ | MCR_HALT);
	return wait_mcr(dev, MCR_FRZACK, MCR_FRZACK);
}

static int unfreeze(const struct device *dev)
{
	wr(dev, FC_MCR, rd(dev, FC_MCR) & ~(MCR_FRZ | MCR_HALT));
	return wait_mcr(dev, MCR_FRZACK | MCR_NOTRDY, 0U);
}

/* Bit timing and mode into CTRL1 (frozen) */
static void write_ctrl1(const struct device *dev)
{
	struct xen_flexcan_data *data = dev->data;
	const struct can_timing *t = &data->timing;
	uint32_t ctrl1 = CTRL1_PRESDIV(t->prescaler - 1U) | CTRL1_RJW(t->sjw - 1U) |
			 CTRL1_PSEG1(t->phase_seg1 - 1U) | CTRL1_PSEG2(t->phase_seg2 - 1U) |
			 CTRL1_PROPSEG(t->prop_seg - 1U) | CTRL1_LBUF | CTRL1_BOFFMSK |
			 CTRL1_TWRNMSK | CTRL1_RWRNMSK;

	if ((data->common.mode & CAN_MODE_LOOPBACK) != 0U) {
		ctrl1 |= CTRL1_LPB;
	}
	if ((data->common.mode & CAN_MODE_LISTENONLY) != 0U) {
		ctrl1 |= CTRL1_LOM;
	}
	wr(dev, FC_CTRL1, ctrl1);
	/* Own frames are received only in loopback mode */
	if ((data->common.mode & CAN_MODE_LOOPBACK) != 0U) {
		wr(dev, FC_MCR, rd(dev, FC_MCR) & ~MCR_SRXDIS);
	} else {
		wr(dev, FC_MCR, rd(dev, FC_MCR) | MCR_SRXDIS);
	}
}

/* Receive mailbox and individual mask of filter @p i (frozen for RXIMR) */
static void write_filter(const struct device *dev, int i)
{
	struct xen_flexcan_data *data = dev->data;
	const struct can_filter *f = &data->filters[i].filter;
	int mb = MB_RX_FIRST + i;
	bool ide = (f->flags & CAN_FILTER_IDE) != 0U;

	wr(dev, FC_MB(mb), CS_CODE(CODE_RX_INACTIVE));
	if (data->filters[i].callback == NULL) {
		wr(dev, FC_RXIMR(mb), 0U);
		return;
	}
	wr(dev, FC_RXIMR(mb), ide ? (f->mask & CAN_EXT_ID_MASK)
				  : ((f->mask & CAN_STD_ID_MASK) << ID_STD_SHIFT));
	wr(dev, FC_MB(mb) + 4, ide ? (f->id & CAN_EXT_ID_MASK)
				   : ((f->id & CAN_STD_ID_MASK) << ID_STD_SHIFT));
	wr(dev, FC_MB(mb), CS_CODE(CODE_RX_EMPTY) | (ide ? CS_IDE : 0U));
}

static enum can_state read_state(const struct device *dev, struct can_bus_err_cnt *cnt)
{
	struct xen_flexcan_data *data = dev->data;
	uint32_t ecr = rd(dev, FC_ECR);
	uint32_t esr1 = rd(dev, FC_ESR1);
	uint8_t tx = ecr & 0xffU, rx = (ecr >> 8) & 0xffU;

	if (cnt != NULL) {
		cnt->tx_err_cnt = tx;
		cnt->rx_err_cnt = rx;
	}
	if (!data->common.started) {
		return CAN_STATE_STOPPED;
	}
	if (ESR1_FLTCONF(esr1) >= 2U) {
		return CAN_STATE_BUS_OFF;
	}
	if (ESR1_FLTCONF(esr1) == 1U) {
		return CAN_STATE_ERROR_PASSIVE;
	}
	return ((tx >= 96U) || (rx >= 96U)) ? CAN_STATE_ERROR_WARNING : CAN_STATE_ERROR_ACTIVE;
}

static void rx_mailbox(const struct device *dev, int i)
{
	struct xen_flexcan_data *data = dev->data;
	int mb = MB_RX_FIRST + i;
	struct can_frame frame = {0};
	uint32_t cs = rd(dev, FC_MB(mb)); /* locks the mailbox */
	uint32_t id = rd(dev, FC_MB(mb) + 4);
	uint32_t w0 = rd(dev, FC_MB(mb) + 8);
	uint32_t w1 = rd(dev, FC_MB(mb) + 12);

	(void)rd(dev, FC_TIMER); /* unlocks the mailbox */
	wr(dev, FC_IFLAG1, BIT(mb));

	if ((cs & CS_IDE) != 0U) {
		frame.flags |= CAN_FRAME_IDE;
		frame.id = id & CAN_EXT_ID_MASK;
	} else {
		frame.id = (id >> ID_STD_SHIFT) & CAN_STD_ID_MASK;
	}
	if ((cs & CS_RTR) != 0U) {
		frame.flags |= CAN_FRAME_RTR;
	}
	frame.dlc = MIN(CS_DLC_GET(cs), 8U);
	/* Payload bytes are big endian within the words */
	sys_put_be32(w0, &frame.data[0]);
	sys_put_be32(w1, &frame.data[4]);

	if (data->filters[i].callback != NULL) {
		data->filters[i].callback(dev, &frame, data->filters[i].user_data);
	}
}

static void xen_flexcan_isr(const struct device *dev)
{
	struct xen_flexcan_data *data = dev->data;
	uint32_t iflag = rd(dev, FC_IFLAG1) & rd(dev, FC_IMASK1);
	uint32_t esr1 = rd(dev, FC_ESR1);

	for (int i = 0; i < NUM_FILTERS; i++) {
		if ((iflag & BIT(MB_RX_FIRST + i)) != 0U) {
			rx_mailbox(dev, i);
		}
	}

	if ((iflag & BIT(MB_TX)) != 0U) {
		can_tx_callback_t cb = data->tx_callback;

		wr(dev, FC_IFLAG1, BIT(MB_TX));
		data->tx_callback = NULL;
		if (cb != NULL) {
			cb(dev, 0, data->tx_user_data);
		}
		k_sem_give(&data->tx_idle);
	}

	if ((esr1 & (ESR1_STATE_INTS | ESR1_ERRINT)) != 0U) {
		struct can_bus_err_cnt cnt;
		enum can_state state;

		wr(dev, FC_ESR1, esr1 & (ESR1_STATE_INTS | ESR1_ERRINT));
		state = read_state(dev, &cnt);
		if (state != data->state) {
			data->state = state;
			can_fire_state_change_callbacks(dev, state, cnt);
		}
	}
}

static int xen_flexcan_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	ARG_UNUSED(dev);

	*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY;
	return 0;
}

static int xen_flexcan_set_timing(const struct device *dev, const struct can_timing *timing)
{
	struct xen_flexcan_data *data = dev->data;

	if (data->common.started) {
		return -EBUSY;
	}
	data->timing = *timing;
	return 0;
}

static int xen_flexcan_set_mode(const struct device *dev, can_mode_t mode)
{
	struct xen_flexcan_data *data = dev->data;

	if (data->common.started) {
		return -EBUSY;
	}
	if ((mode & ~(CAN_MODE_LOOPBACK | CAN_MODE_LISTENONLY)) != 0U) {
		return -ENOTSUP;
	}
	data->common.mode = mode;
	return 0;
}

static int xen_flexcan_start(const struct device *dev)
{
	struct xen_flexcan_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->common.started) {
		ret = -EALREADY;
		goto out;
	}
	ret = freeze(dev);
	if (ret == 0) {
		write_ctrl1(dev);
		wr(dev, FC_ECR, 0U);
		wr(dev, FC_ESR1, UINT32_MAX);
		wr(dev, FC_IFLAG1, UINT32_MAX);
		wr(dev, FC_IMASK1, GENMASK(MB_LAST, MB_RX_FIRST));
		data->common.started = true;
		ret = unfreeze(dev);
	}
	if (ret != 0) {
		data->common.started = false;
		LOG_ERR("start failed (MCR 0x%08x)", rd(dev, FC_MCR));
	}
	data->state = read_state(dev, NULL);
out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int xen_flexcan_stop(const struct device *dev)
{
	struct xen_flexcan_data *data = dev->data;
	can_tx_callback_t cb;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->common.started) {
		k_mutex_unlock(&data->lock);
		return -EALREADY;
	}
	ret = freeze(dev);
	wr(dev, FC_IMASK1, 0U);
	data->common.started = false;
	data->state = CAN_STATE_STOPPED;

	/* Abort a pending transmission */
	wr(dev, FC_MB(MB_TX), CS_CODE(CODE_TX_INACTIVE));
	cb = data->tx_callback;
	data->tx_callback = NULL;
	if (cb != NULL) {
		cb(dev, -ENETDOWN, data->tx_user_data);
		k_sem_give(&data->tx_idle);
	}
	k_mutex_unlock(&data->lock);
	return ret;
}

static int xen_flexcan_send(const struct device *dev, const struct can_frame *frame,
			    k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	struct xen_flexcan_data *data = dev->data;
	bool ide = (frame->flags & CAN_FRAME_IDE) != 0U;
	uint32_t cs;

	if ((frame->flags & ~(CAN_FRAME_IDE | CAN_FRAME_RTR)) != 0U) {
		return -ENOTSUP;
	}
	if (frame->dlc > 8U) {
		return -EINVAL;
	}
	if (!data->common.started) {
		return -ENETDOWN;
	}
	if (data->state == CAN_STATE_BUS_OFF) {
		return -ENETUNREACH;
	}
	if (k_sem_take(&data->tx_idle, timeout) != 0) {
		return -EAGAIN;
	}

	data->tx_callback = callback;
	data->tx_user_data = user_data;

	cs = CS_CODE(CODE_TX_DATA) | CS_DLC(frame->dlc) | (ide ? (CS_IDE | CS_SRR) : 0U) |
	     (((frame->flags & CAN_FRAME_RTR) != 0U) ? CS_RTR : 0U);
	wr(dev, FC_MB(MB_TX), CS_CODE(CODE_TX_INACTIVE));
	wr(dev, FC_MB(MB_TX) + 4, ide ? (frame->id & CAN_EXT_ID_MASK)
				      : ((frame->id & CAN_STD_ID_MASK) << ID_STD_SHIFT));
	wr(dev, FC_MB(MB_TX) + 8, sys_get_be32(&frame->data[0]));
	wr(dev, FC_MB(MB_TX) + 12, sys_get_be32(&frame->data[4]));
	wr(dev, FC_MB(MB_TX), cs);
	/* Erratum ERR005829: write the reserved mailbox twice */
	wr(dev, FC_MB(MB_RESERVED), CS_CODE(CODE_TX_INACTIVE));
	wr(dev, FC_MB(MB_RESERVED), CS_CODE(CODE_TX_INACTIVE));

	return 0;
}

static int xen_flexcan_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				     void *user_data, const struct can_filter *filter)
{
	struct xen_flexcan_data *data = dev->data;
	int id = -ENOSPC;

	if ((filter->flags & ~CAN_FILTER_IDE) != 0U) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	for (int i = 0; i < NUM_FILTERS; i++) {
		if (data->filters[i].callback == NULL) {
			id = i;
			break;
		}
	}
	if (id >= 0) {
		data->filters[id].filter = *filter;
		data->filters[id].user_data = user_data;
		data->filters[id].callback = callback;
		/* RXIMR can only be written frozen */
		if (data->common.started) {
			(void)freeze(dev);
		}
		write_filter(dev, id);
		if (data->common.started) {
			(void)unfreeze(dev);
		}
	}
	k_mutex_unlock(&data->lock);
	return id;
}

static void xen_flexcan_remove_rx_filter(const struct device *dev, int filter_id)
{
	struct xen_flexcan_data *data = dev->data;

	if ((filter_id < 0) || (filter_id >= NUM_FILTERS)) {
		return;
	}
	k_mutex_lock(&data->lock, K_FOREVER);
	data->filters[filter_id].callback = NULL;
	wr(dev, FC_MB(MB_RX_FIRST + filter_id), CS_CODE(CODE_RX_INACTIVE));
	k_mutex_unlock(&data->lock);
}

static int xen_flexcan_get_state(const struct device *dev, enum can_state *state,
				 struct can_bus_err_cnt *err_cnt)
{
	enum can_state s = read_state(dev, err_cnt);

	if (state != NULL) {
		*state = s;
	}
	return 0;
}

static int xen_flexcan_get_core_clock(const struct device *dev, uint32_t *rate)
{
	*rate = DEV_CFG(dev)->fclk;
	return 0;
}

static int xen_flexcan_get_max_filters(const struct device *dev, bool ide)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ide);

	return NUM_FILTERS;
}

static int xen_flexcan_init(const struct device *dev)
{
	const struct xen_flexcan_config *cfg = dev->config;
	struct xen_flexcan_data *data = dev->data;
	struct can_timing timing = {0};
	uint32_t reg;
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);
	k_mutex_init(&data->lock);
	k_sem_init(&data->tx_idle, 1, 1);
	sys_slist_init(&data->common.state_change_callbacks);
	data->state = CAN_STATE_STOPPED;

	/* Enable the module, soft reset, freeze */
	wr(dev, FC_MCR, rd(dev, FC_MCR) & ~MCR_MDIS);
	ret = wait_mcr(dev, MCR_LPMACK, 0U);
	if (ret == 0) {
		wr(dev, FC_MCR, MCR_SOFTRST);
		ret = wait_mcr(dev, MCR_SOFTRST, 0U);
	}
	if (ret == 0) {
		ret = freeze(dev);
	}
	if (ret != 0) {
		LOG_ERR("no response (MCR 0x%08x)", rd(dev, FC_MCR));
		return ret;
	}

	/* ECC: initialize the mailbox RAM and its masks */
	wr(dev, FC_CTRL2, rd(dev, FC_CTRL2) | CTRL2_WRMFRZ);
	for (uint32_t off = FC_RAM_START; off < FC_RAM_END; off += 4U) {
		wr(dev, off, 0U);
	}
	wr(dev, FC_CTRL2, rd(dev, FC_CTRL2) & ~CTRL2_WRMFRZ);

	/* Individual masks, classic CAN, mailboxes 0..MB_LAST */
	wr(dev, FC_MCR, MCR_FRZ | MCR_HALT | MCR_SUPV | MCR_WRNEN | MCR_IRMQ | MCR_SRXDIS |
				MCR_IDAM_C | MCR_MAXMB(MB_LAST));
	/* Remote frames are received, not answered */
	wr(dev, FC_CTRL2, rd(dev, FC_CTRL2) | CTRL2_RRS);

	wr(dev, FC_MB(MB_RESERVED), CS_CODE(CODE_TX_INACTIVE));
	wr(dev, FC_MB(MB_TX), CS_CODE(CODE_TX_INACTIVE));
	for (int i = 0; i < NUM_FILTERS; i++) {
		write_filter(dev, i);
	}
	wr(dev, FC_RXMGMASK, 0U);
	wr(dev, FC_RX14MASK, 0U);
	wr(dev, FC_RX15MASK, 0U);
	wr(dev, FC_RXFGMASK, 0U);

	/* No memory error interrupts or freeze (Linux: QUIRK_DISABLE_MECR) */
	wr(dev, FC_CTRL2, rd(dev, FC_CTRL2) | CTRL2_ECRWRE);
	reg = rd(dev, FC_MECR) & ~MECR_ECRWRDIS;
	wr(dev, FC_MECR, reg);
	reg &= ~(MECR_NCEFAFRZ | MECR_HANCEI_MSK | MECR_FANCEI_MSK);
	wr(dev, FC_MECR, reg);
	wr(dev, FC_MECR, reg | MECR_ECRWRDIS);
	wr(dev, FC_CTRL2, rd(dev, FC_CTRL2) & ~CTRL2_ECRWRE);

	/* Bit rate and sample point from the devicetree */
	ret = can_calc_timing(dev, &timing, cfg->common.bitrate, cfg->common.sample_point);
	if (ret < 0) {
		LOG_ERR("no timing for %u bit/s from %u Hz", cfg->common.bitrate, cfg->fclk);
		return ret;
	}
	data->timing = timing;

	cfg->irq_config(dev);

	LOG_INF("FlexCAN at 0x%lx, %u bit/s (prescaler %u, %u+%u+%u tq)",
		(unsigned long)DEVICE_MMIO_NAMED_GET(dev, reg_base), cfg->common.bitrate,
		timing.prescaler, timing.prop_seg, timing.phase_seg1, timing.phase_seg2);
	return 0;
}

static DEVICE_API(can, xen_flexcan_api) = {
	.get_capabilities = xen_flexcan_get_capabilities,
	.start = xen_flexcan_start,
	.stop = xen_flexcan_stop,
	.set_mode = xen_flexcan_set_mode,
	.set_timing = xen_flexcan_set_timing,
	.send = xen_flexcan_send,
	.add_rx_filter = xen_flexcan_add_rx_filter,
	.remove_rx_filter = xen_flexcan_remove_rx_filter,
	.get_state = xen_flexcan_get_state,
	.get_core_clock = xen_flexcan_get_core_clock,
	.get_max_filters = xen_flexcan_get_max_filters,
	/* CTRL1 fields */
	.timing_min = {.sjw = 1, .prop_seg = 1, .phase_seg1 = 1, .phase_seg2 = 2, .prescaler = 1},
	.timing_max = {.sjw = 4, .prop_seg = 8, .phase_seg1 = 8, .phase_seg2 = 8, .prescaler = 256},
};

#define XEN_FLEXCAN_INIT(n)                                                                        \
	static void xen_flexcan_irq_config_##n(const struct device *dev)                           \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), xen_flexcan_isr,            \
			    DEVICE_DT_INST_GET(n), DT_INST_IRQ(n, flags));                         \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
	static const struct xen_flexcan_config xen_flexcan_config_##n = {                          \
		.common = CAN_DT_DRIVER_CONFIG_INST_GET(n, 0, 1000000),                            \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)),                              \
		.fclk = DT_INST_PROP(n, functional_clock_frequency),                               \
		.irq_config = xen_flexcan_irq_config_##n,                                          \
	};                                                                                         \
	static struct xen_flexcan_data xen_flexcan_data_##n;                                       \
	CAN_DEVICE_DT_INST_DEFINE(n, xen_flexcan_init, NULL, &xen_flexcan_data_##n,                \
				  &xen_flexcan_config_##n, POST_KERNEL, CONFIG_CAN_INIT_PRIORITY,  \
				  &xen_flexcan_api);

DT_INST_FOREACH_STATUS_OKAY(XEN_FLEXCAN_INIT)
