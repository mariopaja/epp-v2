/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Xenstore client for a Zephyr Xen DomU: one request at a time over the
 * xenstore ring page (struct xenstore_domain_interface), notified through the
 * xenstore event channel.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/util.h>
#include <zephyr/xen/events.h>
#include <zephyr/xen/generic.h>
#include <zephyr/xen/hvm.h>

#include <xen/public/io/xs_wire.h>
#include <xen/public/xen.h>

#include <epp/xen/xenstore.h>

LOG_MODULE_REGISTER(epp_xenstore, LOG_LEVEL_INF);

/* Wait between checks while no event arrives */
#define XS_POLL_MS 10
/* Upper bound for a single request/response round trip */
#define XS_REQUEST_TIMEOUT K_SECONDS(5)

static struct xenstore_domain_interface *intf;
static evtchn_port_t xs_port;
static uint32_t xs_req_id;
static K_SEM_DEFINE(xs_event, 0, 1);
static K_MUTEX_DEFINE(xs_lock);
static char xs_payload[XENSTORE_PAYLOAD_MAX + 1];

static void xs_event_cb(void *priv)
{
	ARG_UNUSED(priv);
	k_sem_give(&xs_event);
}

static int xs_ring_write(const void *data, size_t len, k_timepoint_t end)
{
	const uint8_t *src = data;

	while (len > 0U) {
		XENSTORE_RING_IDX cons = intf->req_cons;
		XENSTORE_RING_IDX prod = intf->req_prod;
		size_t used, space, offset, chunk;

		barrier_dmem_fence_full();
		used = prod - cons;
		if (used > XENSTORE_RING_SIZE) {
			return -EIO;
		}

		space = XENSTORE_RING_SIZE - used;
		if (space == 0U) {
			if (sys_timepoint_expired(end)) {
				return -ETIMEDOUT;
			}
			notify_evtchn(xs_port);
			(void)k_sem_take(&xs_event, K_MSEC(XS_POLL_MS));
			continue;
		}

		offset = MASK_XENSTORE_IDX(prod);
		chunk = MIN(MIN(len, space), XENSTORE_RING_SIZE - offset);
		memcpy(&intf->req[offset], src, chunk);
		barrier_dmem_fence_full();
		intf->req_prod = prod + chunk;

		src += chunk;
		len -= chunk;
	}

	notify_evtchn(xs_port);
	return 0;
}

static int xs_ring_read(void *data, size_t len, k_timepoint_t end)
{
	uint8_t *dst = data;

	while (len > 0U) {
		XENSTORE_RING_IDX cons = intf->rsp_cons;
		XENSTORE_RING_IDX prod = intf->rsp_prod;
		size_t avail, offset, chunk;

		barrier_dmem_fence_full();
		avail = prod - cons;
		if (avail > XENSTORE_RING_SIZE) {
			return -EIO;
		}

		if (avail == 0U) {
			if (sys_timepoint_expired(end)) {
				return -ETIMEDOUT;
			}
			(void)k_sem_take(&xs_event, K_MSEC(XS_POLL_MS));
			continue;
		}

		offset = MASK_XENSTORE_IDX(cons);
		chunk = MIN(MIN(len, avail), XENSTORE_RING_SIZE - offset);
		memcpy(dst, &intf->rsp[offset], chunk);
		barrier_dmem_fence_full();
		intf->rsp_cons = cons + chunk;

		dst += chunk;
		len -= chunk;
	}

	notify_evtchn(xs_port);
	return 0;
}

static int xs_error_to_errno(const char *err)
{
	for (size_t i = 0U; i < ARRAY_SIZE(xsd_errors); i++) {
		if (strcmp(err, xsd_errors[i].errstring) == 0) {
			return xsd_errors[i].errnum;
		}
	}

	return EIO;
}

/*
 * Send one request (path, optional value) and wait for its reply. The reply
 * payload is left in xs_payload (NUL-terminated); returns its length.
 */
static int xs_talk(enum xsd_sockmsg_type type, const char *path, const char *value)
{
	k_timepoint_t end = sys_timepoint_calc(XS_REQUEST_TIMEOUT);
	size_t path_len = strlen(path) + 1U;
	size_t value_len = (value != NULL) ? strlen(value) : 0U;
	struct xsd_sockmsg msg = {
		.type = type,
		.req_id = ++xs_req_id,
		.tx_id = 0U,
		.len = path_len + value_len,
	};
	struct xsd_sockmsg rsp;
	int ret;

	if (intf == NULL) {
		return -ENODEV;
	}

	if (msg.len > XENSTORE_PAYLOAD_MAX) {
		return -E2BIG;
	}

	ret = xs_ring_write(&msg, sizeof(msg), end);
	if (ret == 0) {
		ret = xs_ring_write(path, path_len, end);
	}
	if ((ret == 0) && (value_len > 0U)) {
		ret = xs_ring_write(value, value_len, end);
	}
	if (ret != 0) {
		return ret;
	}

	while (true) {
		ret = xs_ring_read(&rsp, sizeof(rsp), end);
		if (ret != 0) {
			return ret;
		}
		if (rsp.len > XENSTORE_PAYLOAD_MAX) {
			return -EIO;
		}
		ret = xs_ring_read(xs_payload, rsp.len, end);
		if (ret != 0) {
			return ret;
		}
		xs_payload[rsp.len] = '\0';

		/* Watch events and stale replies are not for this request */
		if ((rsp.type == XS_WATCH_EVENT) || (rsp.req_id != msg.req_id)) {
			continue;
		}
		if (rsp.type == XS_ERROR) {
			return -xs_error_to_errno(xs_payload);
		}

		return (int)rsp.len;
	}
}

int epp_xs_init(k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	uint64_t evtchn = 0U;
	uint64_t pfn = 0U;
	mm_reg_t va;
	int ret;

	if (intf != NULL) {
		return 0;
	}

	ret = hvm_get_parameter(HVM_PARAM_STORE_EVTCHN, DOMID_SELF, &evtchn);
	if ((ret != 0) || (evtchn == 0U)) {
		LOG_ERR("no xenstore event channel (domain without xen,enhanced?)");
		return -ENODEV;
	}
	xs_port = (evtchn_port_t)evtchn;

	/* dom0less: the page is allocated later by init-dom0less in Dom0 */
	while (true) {
		ret = hvm_get_parameter(HVM_PARAM_STORE_PFN, DOMID_SELF, &pfn);
		if ((ret == 0) && (pfn != 0U) && (pfn != ~0ULL)) {
			break;
		}
		if (sys_timepoint_expired(end)) {
			LOG_ERR("xenstore page not set up (is init-dom0less running in Dom0?)");
			return -ETIMEDOUT;
		}
		k_msleep(200);
	}

	device_map(&va, (uintptr_t)(pfn << XEN_PAGE_SHIFT), XEN_PAGE_SIZE,
		   K_MEM_CACHE_WB | K_MEM_PERM_RW);
	intf = (struct xenstore_domain_interface *)va;

	bind_event_channel(xs_port, xs_event_cb, NULL);
	unmask_event_channel(xs_port);

	while (intf->connection != XENSTORE_CONNECTED) {
		if (sys_timepoint_expired(end)) {
			LOG_ERR("xenstore connection not established");
			intf = NULL;
			return -ETIMEDOUT;
		}
		k_msleep(50);
	}

	LOG_INF("xenstore connected (pfn 0x%llx, evtchn %u)", pfn, xs_port);
	return 0;
}

int epp_xs_read(const char *path, char *buf, size_t len)
{
	int ret;

	k_mutex_lock(&xs_lock, K_FOREVER);
	ret = xs_talk(XS_READ, path, NULL);
	if ((ret >= 0) && (len > 0U)) {
		strncpy(buf, xs_payload, len - 1U);
		buf[len - 1U] = '\0';
	}
	k_mutex_unlock(&xs_lock);

	return ret;
}

int epp_xs_write(const char *path, const char *value)
{
	int ret;

	k_mutex_lock(&xs_lock, K_FOREVER);
	ret = xs_talk(XS_WRITE, path, value);
	k_mutex_unlock(&xs_lock);

	return (ret < 0) ? ret : 0;
}

int epp_xs_directory(const char *path, char *buf, size_t len)
{
	int ret;

	k_mutex_lock(&xs_lock, K_FOREVER);
	ret = xs_talk(XS_DIRECTORY, path, NULL);
	if ((ret >= 0) && (len > 0U)) {
		size_t n = MIN((size_t)ret, len - 1U);

		memcpy(buf, xs_payload, n);
		buf[n] = '\0';
		ret = (int)n;
	}
	k_mutex_unlock(&xs_lock);

	return ret;
}
