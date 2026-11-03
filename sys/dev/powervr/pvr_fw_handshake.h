/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD common FWIF compatibility check, AI-assisted. */
#ifndef _PVR_FW_HANDSHAKE_H
#define _PVR_FW_HANDSHAKE_H
#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif
int pvr_fw_handshake_check(uint8_t, uint8_t, uint32_t, uint32_t,
    uint64_t, uint64_t, uint64_t);
#endif
