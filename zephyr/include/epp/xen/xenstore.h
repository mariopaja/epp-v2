/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Xenstore client for a Zephyr Xen DomU.
 */

#ifndef EPP_XEN_XENSTORE_H_
#define EPP_XEN_XENSTORE_H_

#include <stddef.h>
#include <zephyr/kernel.h>

/**
 * @brief Connect to xenstore.
 *
 * Waits until Xen/Dom0 provide the xenstore page (dom0less: set up late by
 * init-dom0less in Dom0) and the connection is established.
 *
 * @param timeout How long to wait for xenstore.
 * @retval 0 Connected.
 * @retval -ENODEV The domain has no xenstore (no "xen,enhanced").
 * @retval -ETIMEDOUT Xenstore was not set up in time.
 */
int epp_xs_init(k_timeout_t timeout);

/**
 * @brief Read a xenstore node. Relative paths are relative to the domain home.
 * @return Length of the value (NUL-terminated in @p buf), or negative errno.
 */
int epp_xs_read(const char *path, char *buf, size_t len);

/**
 * @brief Write a xenstore node.
 * @return 0 on success, or negative errno.
 */
int epp_xs_write(const char *path, const char *value);

/**
 * @brief List the children of a xenstore node.
 * @return Length of the NUL-separated name list in @p buf, or negative errno.
 */
int epp_xs_directory(const char *path, char *buf, size_t len);

#endif /* EPP_XEN_XENSTORE_H_ */
