/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Xen PV network frontend (netfront) for a Zephyr DomU: one queue, one
 * 4 KiB buffer page per packet (no scatter-gather, no offloads), receive
 * in rx-copy mode. The backend is the vif of Dom0 (netback), bridged to
 * the physical network there.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/xen/events.h>
#include <zephyr/xen/generic.h>
#include <zephyr/xen/gnttab.h>

/* ring.h leaves the barriers to the OS for current interface versions */
#define xen_mb()  barrier_dmem_fence_full()
#define xen_rmb() barrier_dmem_fence_full()
#define xen_wmb() barrier_dmem_fence_full()
#include <xen/public/io/netif.h>
#include <xen/public/io/ring.h>

#include <epp/xen/xenstore.h>

LOG_MODULE_REGISTER(epp_netfront, CONFIG_EPP_XEN_NETFRONT_LOG_LEVEL);

#define NF_RX_BUFS CONFIG_EPP_XEN_NETFRONT_RX_BUFS
#define NF_TX_BUFS CONFIG_EPP_XEN_NETFRONT_TX_BUFS
#define NF_VIF     "device/vif/0"

/* XenbusState values (xen/public/io/xenbus.h) */
enum {
	XB_INITIALISING = 1,
	XB_INITWAIT = 2,
	XB_CONNECTED = 4,
	XB_CLOSING = 5,
	XB_CLOSED = 6,
};

BUILD_ASSERT(NF_RX_BUFS <= __CONST_RING_SIZE(netif_rx, XEN_PAGE_SIZE));
BUILD_ASSERT(NF_TX_BUFS <= __CONST_RING_SIZE(netif_tx, XEN_PAGE_SIZE));

static uint8_t tx_ring_page[XEN_PAGE_SIZE] __aligned(XEN_PAGE_SIZE);
static uint8_t rx_ring_page[XEN_PAGE_SIZE] __aligned(XEN_PAGE_SIZE);
static uint8_t rx_bufs[NF_RX_BUFS][XEN_PAGE_SIZE] __aligned(XEN_PAGE_SIZE);
static uint8_t tx_bufs[NF_TX_BUFS][XEN_PAGE_SIZE] __aligned(XEN_PAGE_SIZE);

struct netfront_data {
	struct net_if *iface;
	uint8_t mac[6];
	domid_t backend_id;
	char backend[64];
	evtchn_port_t port;
	bool connected;

	netif_tx_front_ring_t txr;
	netif_rx_front_ring_t rxr;
	grant_ref_t rx_gref[NF_RX_BUFS];
	grant_ref_t tx_gref[NF_TX_BUFS];

	/* Free TX buffer ids */
	uint16_t tx_free[NF_TX_BUFS];
	size_t tx_free_cnt;
	struct k_sem tx_slots;
	struct k_mutex tx_lock;
	struct k_sem event;
};

static struct netfront_data nf_data;

K_THREAD_STACK_DEFINE(nf_stack, CONFIG_EPP_XEN_NETFRONT_THREAD_STACK_SIZE);
static struct k_thread nf_thread;

static void nf_event_cb(void *priv)
{
	struct netfront_data *nf = priv;

	k_sem_give(&nf->event);
}

static int xs_read_path(const char *dir, const char *key, char *buf, size_t len)
{
	char path[128];

	snprintf(path, sizeof(path), "%s/%s", dir, key);
	return epp_xs_read(path, buf, len);
}

static int xs_write_int(const char *key, unsigned int val)
{
	char path[64];
	char value[16];

	snprintf(path, sizeof(path), NF_VIF "/%s", key);
	snprintf(value, sizeof(value), "%u", val);
	return epp_xs_write(path, value);
}

static int backend_state(struct netfront_data *nf)
{
	char state[8];

	if (xs_read_path(nf->backend, "state", state, sizeof(state)) < 0) {
		return -1;
	}
	return atoi(state);
}

static int wait_backend_state(struct netfront_data *nf, int want, k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);
	int state;

	while (true) {
		state = backend_state(nf);
		if (state == want) {
			return 0;
		}
		if ((state == XB_CLOSING) || (state == XB_CLOSED)) {
			return -ECONNRESET;
		}
		if (sys_timepoint_expired(end)) {
			LOG_ERR("backend %s in state %d, expected %d", nf->backend, state, want);
			return -ETIMEDOUT;
		}
		k_msleep(100);
	}
}

static int hex_nibble(char c)
{
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}
	if ((c >= 'a') && (c <= 'f')) {
		return c - 'a' + 10;
	}
	if ((c >= 'A') && (c <= 'F')) {
		return c - 'A' + 10;
	}
	return -1;
}

/* "aa:bb:cc:dd:ee:ff" */
static int parse_mac(const char *str, uint8_t mac[6])
{
	for (int i = 0; i < 6; i++) {
		int hi = hex_nibble(str[i * 3]);
		int lo = hex_nibble(str[(i * 3) + 1]);
		char sep = str[(i * 3) + 2];

		if ((hi < 0) || (lo < 0) || ((i < 5) && (sep != ':')) || ((i == 5) && (sep != '\0'))) {
			return -EINVAL;
		}
		mac[i] = (uint8_t)((hi << 4) | lo);
	}
	return 0;
}

static void rx_post(struct netfront_data *nf, uint16_t id)
{
	netif_rx_request_t *req = RING_GET_REQUEST(&nf->rxr, nf->rxr.req_prod_pvt);

	req->id = id;
	req->gref = nf->rx_gref[id];
	nf->rxr.req_prod_pvt++;
}

static void rx_process(struct netfront_data *nf)
{
	RING_IDX cons, prod;
	int more, notify;

	do {
		prod = nf->rxr.sring->rsp_prod;
		xen_rmb();

		for (cons = nf->rxr.rsp_cons; cons != prod; cons++) {
			netif_rx_response_t *rsp = RING_GET_RESPONSE(&nf->rxr, cons);
			uint16_t id = rsp->id;

			if (id >= NF_RX_BUFS) {
				LOG_ERR("bad rx id %u", id);
				continue;
			}

			if ((rsp->status > 0) &&
			    ((rsp->flags & (NETRXF_more_data | NETRXF_extra_info)) == 0U) &&
			    ((rsp->offset + rsp->status) <= XEN_PAGE_SIZE) && nf->connected) {
				struct net_pkt *pkt = net_pkt_rx_alloc_with_buffer(
					nf->iface, rsp->status, NET_AF_UNSPEC, 0, K_NO_WAIT);

				if (pkt == NULL) {
					LOG_WRN("rx: no net_pkt, %d bytes dropped", rsp->status);
				} else if ((net_pkt_write(pkt, &rx_bufs[id][rsp->offset],
							  rsp->status) != 0) ||
					   (net_recv_data(nf->iface, pkt) < 0)) {
					net_pkt_unref(pkt);
				}
			} else if (rsp->status <= 0) {
				LOG_DBG("rx error %d", rsp->status);
			} else {
				LOG_WRN("rx: unsupported packet (flags 0x%x, %d bytes)", rsp->flags,
					rsp->status);
			}

			/* The buffer stays granted: hand it back right away */
			rx_post(nf, id);
		}
		nf->rxr.rsp_cons = cons;

		RING_FINAL_CHECK_FOR_RESPONSES(&nf->rxr, more);
	} while (more);

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&nf->rxr, notify);
	if (notify) {
		notify_evtchn(nf->port);
	}
}

static void tx_process(struct netfront_data *nf)
{
	RING_IDX cons, prod;
	int more;

	k_mutex_lock(&nf->tx_lock, K_FOREVER);
	do {
		prod = nf->txr.sring->rsp_prod;
		xen_rmb();

		for (cons = nf->txr.rsp_cons; cons != prod; cons++) {
			netif_tx_response_t *rsp = RING_GET_RESPONSE(&nf->txr, cons);

			if (rsp->status == NETIF_RSP_NULL) {
				continue;
			}
			if (rsp->status != NETIF_RSP_OKAY) {
				LOG_DBG("tx error %d", rsp->status);
			}
			if (rsp->id < NF_TX_BUFS) {
				nf->tx_free[nf->tx_free_cnt++] = rsp->id;
				k_sem_give(&nf->tx_slots);
			}
		}
		nf->txr.rsp_cons = cons;

		RING_FINAL_CHECK_FOR_RESPONSES(&nf->txr, more);
	} while (more);
	k_mutex_unlock(&nf->tx_lock);
}

static int nf_send(const struct device *dev, struct net_pkt *pkt)
{
	struct netfront_data *nf = dev->data;
	size_t len = net_pkt_get_len(pkt);
	netif_tx_request_t *req;
	uint16_t id;
	int notify;

	if (!nf->connected) {
		return -ENETDOWN;
	}
	if (len > XEN_PAGE_SIZE) {
		return -EMSGSIZE;
	}
	if (k_sem_take(&nf->tx_slots, K_MSEC(100)) != 0) {
		return -ENOBUFS;
	}

	k_mutex_lock(&nf->tx_lock, K_FOREVER);
	id = nf->tx_free[--nf->tx_free_cnt];

	net_pkt_cursor_init(pkt);
	if (net_pkt_read(pkt, tx_bufs[id], len) != 0) {
		nf->tx_free[nf->tx_free_cnt++] = id;
		k_mutex_unlock(&nf->tx_lock);
		k_sem_give(&nf->tx_slots);
		return -EIO;
	}

	req = RING_GET_REQUEST(&nf->txr, nf->txr.req_prod_pvt);
	req->gref = nf->tx_gref[id];
	req->offset = 0U;
	req->flags = 0U;
	req->id = id;
	req->size = (uint16_t)len;
	nf->txr.req_prod_pvt++;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&nf->txr, notify);
	k_mutex_unlock(&nf->tx_lock);

	if (notify) {
		notify_evtchn(nf->port);
	}

	return 0;
}

static int nf_connect(struct netfront_data *nf)
{
	char buf[32];
	int ret;

	k_timepoint_t end = sys_timepoint_calc(K_SECONDS(CONFIG_EPP_XEN_NETFRONT_XENSTORE_TIMEOUT));

	ret = epp_xs_init(K_SECONDS(CONFIG_EPP_XEN_NETFRONT_XENSTORE_TIMEOUT));
	if (ret != 0) {
		return ret;
	}

	/*
	 * dom0less: Dom0 attaches the vif (xl network-attach) only after it
	 * has set up xenstore (init-dom0less), so wait for it to appear.
	 */
	while ((epp_xs_read(NF_VIF "/backend", nf->backend, sizeof(nf->backend)) < 0) ||
	       (epp_xs_read(NF_VIF "/backend-id", buf, sizeof(buf)) < 0)) {
		if (sys_timepoint_expired(end)) {
			LOG_ERR("no " NF_VIF " in xenstore (xl network-attach in Dom0?)");
			return -ENODEV;
		}
		k_msleep(200);
	}
	nf->backend_id = (domid_t)atoi(buf);

	if ((epp_xs_read(NF_VIF "/mac", buf, sizeof(buf)) < 0) || (parse_mac(buf, nf->mac) != 0)) {
		LOG_ERR("no valid " NF_VIF "/mac");
		return -EINVAL;
	}

	LOG_INF("vif backend %s (dom %u), mac %s", nf->backend, nf->backend_id, buf);

	ret = wait_backend_state(nf, XB_INITWAIT, K_SECONDS(30));
	if (ret != 0) {
		return ret;
	}

	/* Shared rings */
	SHARED_RING_INIT((netif_tx_sring_t *)tx_ring_page);
	FRONT_RING_INIT(&nf->txr, (netif_tx_sring_t *)tx_ring_page, XEN_PAGE_SIZE);
	SHARED_RING_INIT((netif_rx_sring_t *)rx_ring_page);
	FRONT_RING_INIT(&nf->rxr, (netif_rx_sring_t *)rx_ring_page, XEN_PAGE_SIZE);

	grant_ref_t tx_ring_ref = gnttab_grant_access(nf->backend_id,
						      xen_virt_to_gfn(tx_ring_page), false);
	grant_ref_t rx_ring_ref = gnttab_grant_access(nf->backend_id,
						      xen_virt_to_gfn(rx_ring_page), false);

	/* Buffers: RX writable by the backend (rx-copy), TX read-only */
	for (uint16_t i = 0U; i < NF_RX_BUFS; i++) {
		nf->rx_gref[i] = gnttab_grant_access(nf->backend_id,
						     xen_virt_to_gfn(rx_bufs[i]), false);
	}
	for (uint16_t i = 0U; i < NF_TX_BUFS; i++) {
		nf->tx_gref[i] = gnttab_grant_access(nf->backend_id,
						     xen_virt_to_gfn(tx_bufs[i]), true);
		nf->tx_free[i] = i;
	}
	nf->tx_free_cnt = NF_TX_BUFS;
	k_sem_init(&nf->tx_slots, NF_TX_BUFS, NF_TX_BUFS);

	/* Event channel to the backend */
	ret = alloc_unbound_event_channel(nf->backend_id);
	if (ret < 0) {
		LOG_ERR("alloc_unbound_event_channel failed: %d", ret);
		return ret;
	}
	nf->port = (evtchn_port_t)ret;
	bind_event_channel(nf->port, nf_event_cb, nf);
	unmask_event_channel(nf->port);

	if ((xs_write_int("tx-ring-ref", tx_ring_ref) != 0) ||
	    (xs_write_int("rx-ring-ref", rx_ring_ref) != 0) ||
	    (xs_write_int("event-channel", nf->port) != 0) ||
	    (xs_write_int("request-rx-copy", 1U) != 0) ||
	    (xs_write_int("feature-rx-notify", 1U) != 0) ||
	    (xs_write_int("feature-sg", 0U) != 0) ||
	    (xs_write_int("feature-gso-tcpv4", 0U) != 0) ||
	    (xs_write_int("feature-gso-tcpv6", 0U) != 0)) {
		LOG_ERR("writing the vif setup to xenstore failed");
		return -EIO;
	}

	/* Hand all RX buffers to the backend */
	for (uint16_t i = 0U; i < NF_RX_BUFS; i++) {
		rx_post(nf, i);
	}
	int notify;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&nf->rxr, notify);

	ret = xs_write_int("state", XB_CONNECTED);
	if (ret != 0) {
		return ret;
	}
	notify_evtchn(nf->port);

	ret = wait_backend_state(nf, XB_CONNECTED, K_SECONDS(30));
	if (ret != 0) {
		return ret;
	}

	LOG_INF("connected: tx-ring-ref %u, rx-ring-ref %u, evtchn %u", tx_ring_ref, rx_ring_ref,
		nf->port);
	return 0;
}

static void nf_thread_fn(void *p1, void *p2, void *p3)
{
	struct netfront_data *nf = p1;
	int ret;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = nf_connect(nf);
	if (ret != 0) {
		LOG_ERR("vif not connected: %d", ret);
		return;
	}

	/* The stack keeps a copy of the link address: set the vif MAC */
	ret = net_if_set_link_addr(nf->iface, nf->mac, sizeof(nf->mac), NET_LINK_ETHERNET);
	if (ret != 0) {
		LOG_WRN("could not set MAC: %d", ret);
	} else {
		LOG_INF("link address %02x:%02x:%02x:%02x:%02x:%02x", nf->mac[0], nf->mac[1],
			nf->mac[2], nf->mac[3], nf->mac[4], nf->mac[5]);
	}

	nf->connected = true;
	net_eth_carrier_on(nf->iface);

	while (true) {
		/* Events wake us up; the timeout also covers a missed one */
		(void)k_sem_take(&nf->event, K_MSEC(100));
		tx_process(nf);
		rx_process(nf);
	}
}

static void nf_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct netfront_data *nf = dev->data;

	nf->iface = iface;
	/* Placeholder until the MAC from xenstore is known (Xen OUI) */
	nf->mac[0] = 0x00;
	nf->mac[1] = 0x16;
	nf->mac[2] = 0x3e;
	net_if_set_link_addr(iface, nf->mac, sizeof(nf->mac), NET_LINK_ETHERNET);
	ethernet_init(iface);
	net_if_carrier_off(iface);

	k_thread_create(&nf_thread, nf_stack, K_THREAD_STACK_SIZEOF(nf_stack), nf_thread_fn, nf,
			NULL, NULL, CONFIG_EPP_XEN_NETFRONT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&nf_thread, "xen_netfront");
}

static enum ethernet_hw_caps nf_get_capabilities(const struct device *dev,
					      struct net_if *iface)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(iface);
	return 0;
}

static int nf_init(const struct device *dev)
{
	struct netfront_data *nf = dev->data;

	k_sem_init(&nf->event, 0, 1);
	k_mutex_init(&nf->tx_lock);
	return 0;
}

static const struct ethernet_api nf_api = {
	.iface_api.init = nf_iface_init,
	.get_capabilities = nf_get_capabilities,
	.send = nf_send,
};

ETH_NET_DEVICE_INIT(xen_netfront, "xen-netfront", nf_init, NULL, &nf_data, NULL,
		    CONFIG_ETH_INIT_PRIORITY, &nf_api, NET_ETH_MTU);
