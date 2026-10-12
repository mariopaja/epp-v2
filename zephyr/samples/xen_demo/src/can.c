/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Part flexcan2 (CONFIG_EPP_FLEXCAN2): CAN2, connector J20, 500 kbit/s. A
 * heartbeat frame 0x123 every second (counter, big endian); frames with ID
 * 0x100 are answered on 0x101 with the same data; every 10 s the state and
 * counters. With CONFIG_EPP_FLEXCAN2_LOOPBACK (extras/can-loopback.conf) in
 * loopback mode: own frames are received, and every 10 s a request 0x100 is
 * sent to itself.
 */

#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "part.h"

#define CAN_ID_HEARTBEAT 0x123
#define CAN_ID_REQUEST   0x100
#define CAN_ID_ANSWER    0x101

static const struct device *const can_dev = DEVICE_DT_GET(DT_NODELABEL(flexcan2));
static atomic_t on;
static atomic_t tx_count;
static atomic_t rx_count;
static struct can_frame last; /* last received frame, under lock */
static struct k_spinlock lock;
K_MSGQ_DEFINE(requests, sizeof(struct can_frame), 4, 4);

/*
 * Sending never blocks: with a callback can_send() returns as soon as the
 * frame is in the transmit mailbox. Without one it would wait until the
 * frame is sent, which is forever while no other node acknowledges (Linux's
 * can0 down, no wiring): the controller keeps retransmitting.
 */
static void sent(const struct device *dev, int error, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	if (error == 0) {
		atomic_inc(&tx_count);
	}
}

static void answer(struct k_work *work)
{
	struct can_frame frame;

	ARG_UNUSED(work);
	while (k_msgq_get(&requests, &frame, K_NO_WAIT) == 0) {
		frame.id = CAN_ID_ANSWER;
		(void)can_send(can_dev, &frame, K_MSEC(100), sent, NULL);
	}
}

static K_WORK_DEFINE(answer_work, answer);

/* Runs in the driver's interrupt handler */
static void received(const struct device *dev, struct can_frame *frame, void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	last = *frame;
	k_spin_unlock(&lock, key);
	atomic_inc(&rx_count);

	if (((frame->flags & CAN_FRAME_IDE) == 0U) && (frame->id == CAN_ID_REQUEST) &&
	    (k_msgq_put(&requests, frame, K_NO_WAIT) == 0)) {
		k_work_submit(&answer_work);
	}
}

static void can_part_start(void)
{
	const struct can_filter all_std = {.id = 0, .mask = 0, .flags = 0};
	const struct can_filter all_ext = {.id = 0, .mask = 0, .flags = CAN_FILTER_IDE};
	int ret;

	if (!demo_start_device(can_dev, "flexcan2")) {
		return;
	}
	if (IS_ENABLED(CONFIG_EPP_FLEXCAN2_LOOPBACK)) {
		ret = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
		if (ret != 0) {
			printk("app: can: loopback mode failed (%d)\n", ret);
			return;
		}
	}
	if ((can_add_rx_filter(can_dev, received, NULL, &all_std) < 0) ||
	    (can_add_rx_filter(can_dev, received, NULL, &all_ext) < 0)) {
		printk("app: can: adding filters failed\n");
		return;
	}
	ret = can_start(can_dev);
	if (ret != 0) {
		printk("app: can: start failed (%d)\n", ret);
		return;
	}
	atomic_set(&on, 1);
}

static void heartbeat(uint32_t second)
{
	struct can_frame frame = {.id = CAN_ID_HEARTBEAT, .dlc = 4};

	if (!atomic_get(&on)) {
		return;
	}
	sys_put_be32(second, frame.data);
	/* Skipped while the previous frame is still pending */
	(void)can_send(can_dev, &frame, K_NO_WAIT, sent, NULL);
}

static void can_part_report(uint32_t round)
{
	static const char *const names[] = {"error-active", "error-warning", "error-passive",
					    "bus-off", "stopped"};
	struct can_bus_err_cnt cnt = {0};
	enum can_state state = CAN_STATE_STOPPED;
	struct can_frame frame;
	k_spinlock_key_t key;

	ARG_UNUSED(round);
	if (!atomic_get(&on)) {
		return;
	}
	if (IS_ENABLED(CONFIG_EPP_FLEXCAN2_LOOPBACK)) {
		/* A request to ourselves: received, answered on 0x101, received */
		struct can_frame request = {.id = CAN_ID_REQUEST, .dlc = 2, .data = {0xca, 0xfe}};

		(void)can_send(can_dev, &request, K_MSEC(100), sent, NULL);
	}
	(void)can_get_state(can_dev, &state, &cnt);
	key = k_spin_lock(&lock);
	frame = last;
	k_spin_unlock(&lock, key);

	printk("app: can (J20%s): %s (tx err %u, rx err %u), sent %ld, received %ld",
	       IS_ENABLED(CONFIG_EPP_FLEXCAN2_LOOPBACK) ? ", loopback" : "", names[state],
	       cnt.tx_err_cnt, cnt.rx_err_cnt, (long)atomic_get(&tx_count),
	       (long)atomic_get(&rx_count));
	if (atomic_get(&rx_count) > 0) {
		printk(", last 0x%03x [%u]", frame.id, frame.dlc);
		for (uint8_t i = 0U; i < frame.dlc; i++) {
			printk(" %02x", frame.data[i]);
		}
	}
	printk("\n");
}

DEMO_PART(p5_flexcan2, .option = "flexcan2", .start = can_part_start, .every_second = heartbeat,
	  .report = can_part_report);
