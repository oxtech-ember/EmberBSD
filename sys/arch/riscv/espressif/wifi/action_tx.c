/* SPDX-License-Identifier: BSD-2-Clause */
/* Correlate WPA Action requests with IDF's ordinary management TX path. */

#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "../esp_wifi_os.h"

struct action_tx_state {
	const void *request;
	unsigned long request_size;
	void *owner_task;
	void *ioctl_request;
	void *send_task;
	void *tx_mbuf;
	uint32_t cookie;
	uint32_t generation;
	uint8_t bssid[6];
	bool active;
	bool send_active;
	bool output_seen;
	bool output_active;
	bool output_done;
	bool output_ok;
	bool send_done;
	int send_error;
	bool tx_done;
};

static struct action_tx_state action_tx;

static int
action_tx_errno(int error)
{
	if (error == ESP_OK)
		return 0;
	if (error == ESP_ERR_INVALID_ARG)
		return EINVAL;
	if (error == ESP_ERR_NO_MEM)
		return ENOMEM;
	return EIO;
}

extern int __real_esp_wifi_send_mgmt_frm_internal(const void *);
extern int __real_ieee80211_ioctl(void *);
extern int __real_wifi_send_mgmt_frame(void *);
extern int __real_ieee80211_mgmt_output(void *, void *, unsigned int);
extern void __real_ieee80211_tx_mgt_cb(void *);
int __wrap_esp_wifi_send_mgmt_frm_internal(const void *);

static void
action_tx_zero(void *data, unsigned long size)
{
	volatile uint8_t *p = data;

	while (size-- != 0)
		*p++ = 0;
}

static void
action_tx_finish(void)
{
	const void *request;
	unsigned long size;
	uint32_t cookie, generation;
	uint8_t bssid[6];
	int tx_error;
	unsigned int s;

	s = espwifi_os_critical_enter();
	if (!action_tx.active || !action_tx.send_done || action_tx.output_active ||
	    (action_tx.output_seen && !action_tx.output_done) ||
	    (action_tx.output_seen && action_tx.output_ok && !action_tx.tx_done) ||
	    (!action_tx.output_seen && action_tx.send_error == ESP_OK)) {
		espwifi_os_critical_exit(s);
		return;
	}
	request = action_tx.request;
	size = action_tx.request_size;
	cookie = action_tx.cookie;
	generation = action_tx.generation;
	for (unsigned i = 0; i < sizeof(bssid); i++)
		bssid[i] = action_tx.bssid[i];
	tx_error = action_tx_errno(action_tx.send_error);
	memset(&action_tx, 0, sizeof(action_tx));
	espwifi_os_critical_exit(s);

	if (request != NULL) {
		action_tx_zero((void *)request, size);
		espwifi_os_free((void *)request);
	}
	/* This path knows local submission status, but never peer ACK status. */
	espwifi_if_action_tx_done(cookie, generation, bssid, tx_error);
}

int
espwifi_vendor_action_tx_submit(const void *request, unsigned long size,
	    uint32_t cookie, uint32_t generation, const unsigned char *bssid)
{
	unsigned int s;

	if (request == NULL || size == 0 || cookie == 0 || generation == 0 ||
	    bssid == NULL)
		return ESP_ERR_INVALID_ARG;
	s = espwifi_os_critical_enter();
	if (action_tx.active) {
		espwifi_os_critical_exit(s);
		return ESP_ERR_INVALID_STATE;
	}
	memset(&action_tx, 0, sizeof(action_tx));
	action_tx.active = true;
	action_tx.request = request;
	action_tx.request_size = size;
	action_tx.owner_task = espwifi_os_task_current();
	action_tx.cookie = cookie;
	action_tx.generation = generation;
	for (unsigned i = 0; i < sizeof(action_tx.bssid); i++)
		action_tx.bssid[i] = bssid[i];
	espwifi_os_critical_exit(s);

	/* Once armed, the drain callback is the terminal contract. */
	(void)__wrap_esp_wifi_send_mgmt_frm_internal(request);
	return ESP_OK;
}

int
__wrap_esp_wifi_send_mgmt_frm_internal(const void *request)
{
	unsigned int s;
	bool target;
	int error;

	s = espwifi_os_critical_enter();
	target = action_tx.active && request == action_tx.request &&
	    !action_tx.send_active;
	if (target) {
		action_tx.send_active = true;
		action_tx.owner_task = espwifi_os_task_current();
		action_tx.ioctl_request = NULL;
		action_tx.send_task = NULL;
		action_tx.tx_mbuf = NULL;
	}
	espwifi_os_critical_exit(s);

	error = __real_esp_wifi_send_mgmt_frm_internal(request);
	if (target) {
		s = espwifi_os_critical_enter();
		action_tx.send_active = false;
		action_tx.send_done = true;
		action_tx.send_error = error;
		espwifi_os_critical_exit(s);
		action_tx_finish();
	}
	return error;
}

int
__wrap_ieee80211_ioctl(void *request)
{
	unsigned int s;

	s = espwifi_os_critical_enter();
	if (action_tx.send_active &&
	    action_tx.owner_task == espwifi_os_task_current() &&
	    action_tx.ioctl_request == NULL)
		action_tx.ioctl_request = request;
	espwifi_os_critical_exit(s);
	return __real_ieee80211_ioctl(request);
}

int
__wrap_wifi_send_mgmt_frame(void *request)
{
	unsigned int s;
	bool target;

	s = espwifi_os_critical_enter();
	target = action_tx.send_active && request != NULL &&
	    request == action_tx.ioctl_request;
	if (target)
		action_tx.send_task = espwifi_os_task_current();
	espwifi_os_critical_exit(s);
	return __real_wifi_send_mgmt_frame(request);
}

int
__wrap_ieee80211_mgmt_output(void *node, void *mbuf, unsigned int subtype)
{
	unsigned int s;
	bool target;
	int error;

	s = espwifi_os_critical_enter();
	target = action_tx.send_active && mbuf != NULL &&
	    action_tx.send_task == espwifi_os_task_current() &&
	    /* Pinned S31 ABI passes the encoded FC subtype byte (0xd0). */
	    subtype == (13U << 4) &&
	    !action_tx.output_seen && !action_tx.output_active &&
	    action_tx.tx_mbuf == NULL;
	if (target) {
		action_tx.tx_mbuf = mbuf;
		action_tx.output_active = true;
	}
	espwifi_os_critical_exit(s);

	error = __real_ieee80211_mgmt_output(node, mbuf, subtype);
	if (target) {
		s = espwifi_os_critical_enter();
		action_tx.output_seen = true;
		action_tx.output_active = false;
		action_tx.output_done = true;
		action_tx.output_ok = error == 0;
		if (error != 0)
			action_tx.tx_mbuf = NULL;
		espwifi_os_critical_exit(s);
		action_tx_finish();
	}
	return error;
}

void
__wrap_ieee80211_tx_mgt_cb(void *mbuf)
{
	unsigned int s;
	bool target;

	s = espwifi_os_critical_enter();
	target = action_tx.active && mbuf != NULL &&
	    action_tx.tx_mbuf == mbuf;
	espwifi_os_critical_exit(s);

	/* IDF owns and frees the mbuf in the original callback. */
	__real_ieee80211_tx_mgt_cb(mbuf);
	if (!target)
		return;

	s = espwifi_os_critical_enter();
	if (action_tx.active && action_tx.tx_mbuf == mbuf)
		action_tx.tx_done = true;
	espwifi_os_critical_exit(s);
	action_tx_finish();
}
