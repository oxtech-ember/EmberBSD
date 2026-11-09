/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD Realtek Bluetooth patch loader, AI-assisted. */
/*
 * Realtek Bluetooth patch image format and download fragmentation.
 *
 * The format facts come from FreeBSD rtlbtfw(8) (BSD-2-Clause; Adrian Chadd,
 * Vladimir Kondratyev, Future Crew LLC). This file contains no FreeBSD code.
 * It has no kernel or socket dependencies, so host tests can compile it.
 */

#ifndef RTLBT_FW_H
#define RTLBT_FW_H

#include <stddef.h>
#include <stdint.h>

/* Bluetooth SIG company identifier of Realtek Semiconductor Corporation. */
#define RTLBT_MANUFACTURER		0x005d

/* Payload bytes of one download fragment (vendor command 0xfc20). */
#define RTLBT_FRAG_LEN			252
#define RTLBT_FRAG_LAST			0x80

/* Local version information of the controller, as reported by HCI. */
struct rtlbt_version {
	uint8_t		hci_version;
	uint16_t	hci_revision;
	uint8_t		lmp_version;
	uint16_t	manufacturer;
	uint16_t	lmp_subversion;
};

/* The chip does not run without its config file. */
#define RTLBT_CHIP_CONFIG		0x01

/* A controller that runs its ROM firmware and accepts a patch. */
struct rtlbt_chip {
	uint16_t	lmp_subversion;
	uint16_t	hci_revision;
	uint8_t		hci_version;
	uint8_t		flags;		/* RTLBT_CHIP_* */
	const char	*name;		/* firmware file prefix, e.g. rtl8821c */
	const char	*fw_suffix;	/* "_fw.bin" or a v2 variant */
};

/* Result of rtlbt_image_build(). The caller frees data with free(3). */
struct rtlbt_image {
	uint8_t		*data;
	size_t		len;
};

const struct rtlbt_chip *rtlbt_chip_lookup(const struct rtlbt_version *);

int	rtlbt_image_build(const uint8_t *, size_t, const uint8_t *, size_t,
	    uint8_t, uint16_t, struct rtlbt_image *, const char **);

size_t	rtlbt_fragment_count(size_t);
size_t	rtlbt_fragment(size_t, size_t, uint8_t *, size_t *);

#endif /* RTLBT_FW_H */
