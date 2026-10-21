/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; freestanding instruction fixture ABI. */
#ifndef AICWF_FIRMWARE_HARNESS_H
#define AICWF_FIRMWARE_HARNESS_H

typedef unsigned char u8;
typedef unsigned int u32;
_Static_assert(sizeof(u8) == 1 && sizeof(u32) == 4 && sizeof(void *) == 4,
    "ARM32 fixture ABI required");

#define VIF_BASE 0x18fb08U
#define VIF_SIZE 0x528U
#define VIF_BYTES (2U * VIF_SIZE)

struct fixture_seed {
	u32 valid, cipher, length, hwindex, incoming_hwindex;
	u32 ptk, igtk, station, rx_vif, rx_flags;
	u32 key_xor, station_flags, backup_word;
};

struct test_case {
	u32 kind, floor_lo, floor_hi, tx_lo, tx_hi;
	u32 keyid, vif, mask, frame_len;
	const u8 *frame;
	u8 request[44];
	const struct fixture_seed *seed;
};

struct call {
	u32 entry, args[5];
};
struct answer {
	u32 r0, r1, primask, abi_bad;
};
struct result {
	u32 id;
	struct answer answer;
	u32 rng_calls, special;
	u8 before[VIF_BYTES], after[VIF_BYTES];
};

void abi_call(const struct call *, volatile struct answer *);
void harness_main(void);
void harness_irq(void);
u32 harness_rng(void);
void harness_assert(void);
#endif
