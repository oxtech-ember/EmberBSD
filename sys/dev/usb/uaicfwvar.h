/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UAICFWVAR_H_
#define _DEV_USB_UAICFWVAR_H_

/* Snapshot the station generation when the first bytes reach the host. */
typedef uint64_t (*uaicfw_serial_fn)(void *);
/* One complete unpadded record. The buffer is valid during the callback. */
typedef void (*uaicfw_input_fn)(void *, const uint8_t *, size_t, uint64_t);

/* Data operations supplied by the station. */
enum uaicfw_data_kind {
	UAICFW_ETHERNET, UAICFW_SAE_AUTH, UAICFW_SA_QUERY
};

/* Check the same port lifetime after a sleeping transfer allocation. */
struct uaicfw_port {
	uint64_t (*current)(void *, enum uaicfw_data_kind);
	void *cookie;
	uint64_t serial;
};

struct uaicfw_attach_args {
	void *cookie;
	int (*write)(void *, const uint8_t *, size_t, bool *);
	int (*data)(void *, enum uaicfw_data_kind, const uint8_t *, size_t,
	    const struct uaicfw_port *, bool *);
	void (*listen)(void *, uaicfw_input_fn, uaicfw_serial_fn, void *);
};
#endif
