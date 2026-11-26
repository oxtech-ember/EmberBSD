/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UAICFWVAR_H_
#define _DEV_USB_UAICFWVAR_H_

/* Input buffers belong to the USB transfer until the callback returns. */
typedef void (*uaicfw_input_fn)(void *, const uint8_t *, size_t);

struct uaicfw_attach_args {
	void *cookie;
	int (*write)(void *, const uint8_t *, size_t, bool *);
	void (*listen)(void *, uaicfw_input_fn, void *);
};
#endif
