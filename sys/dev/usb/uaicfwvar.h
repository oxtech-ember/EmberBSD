/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UAICFWVAR_H_
#define _DEV_USB_UAICFWVAR_H_

/* Snapshot the station generation when the first bytes reach the host. */
typedef uint64_t (*uaicfw_serial_fn)(void *);
/* One complete unpadded record. The buffer is valid during the callback. */
typedef void (*uaicfw_input_fn)(void *, const uint8_t *, size_t, uint64_t);

struct uaicfw_attach_args {
	void *cookie;
	int (*write)(void *, const uint8_t *, size_t, bool *);
	int (*ethernet)(void *, const uint8_t *, size_t, bool *);
	void (*listen)(void *, uaicfw_input_fn, uaicfw_serial_fn, void *);
};
#endif
