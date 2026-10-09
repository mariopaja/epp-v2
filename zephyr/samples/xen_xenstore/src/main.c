/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Step 1 of the Zephyr Xen network frontend: connect to xenstore from a
 * dom0less DomU and read the domain's own nodes. Prints the result every
 * 5 s (the Xen serial console is shared with the other domains).
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <epp/xen/xenstore.h>

static char buf[512];

static void list_dir(const char *path)
{
	int len = epp_xs_directory(path, buf, sizeof(buf));

	if (len < 0) {
		printk("  ls %s: error %d\n", path, len);
		return;
	}

	printk("  ls %s:", path);
	for (int i = 0; i < len; i += (int)strlen(&buf[i]) + 1) {
		printk(" %s", &buf[i]);
	}
	printk("\n");
}

int main(void)
{
	char domid[16] = "?";
	char name[64] = "?";
	char home[48];
	int ret;

	printk("xen_xenstore: waiting for xenstore (Dom0 init-dom0less)...\n");
	ret = epp_xs_init(K_SECONDS(300));
	if (ret != 0) {
		printk("xen_xenstore: no xenstore: %d\n", ret);
		return 0;
	}

	(void)epp_xs_read("domid", domid, sizeof(domid));
	(void)epp_xs_read("name", name, sizeof(name));
	snprintf(home, sizeof(home), "/local/domain/%s", domid);

	ret = epp_xs_write("data/zephyr", "hello from Zephyr");
	printk("xen_xenstore: write data/zephyr: %d\n", ret);

	while (true) {
		printk("xen_xenstore: domid=%s name=%s\n", domid, name);
		list_dir(home);
		list_dir("device");
		k_msleep(5000);
	}

	return 0;
}
