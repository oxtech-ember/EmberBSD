/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_CIXVPU_RING_H_
#define _DEV_IC_CIXVPU_RING_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* Protocol 3 has a 16-byte control area before this data region. */
#define CIXVPU_RING_WORDS		1020U
#define CIXVPU_RING_BYTES		(CIXVPU_RING_WORDS * 4U)
#define CIXVPU_RING_MAX_PAYLOAD	4072U
#define CIXVPU_RING_SUM_MAX_PAYLOAD 4068U

struct cixvpu_ring_checksum {
	uint32_t sum;
	uint32_t reserved[3];
};

/*
 * AI-assisted, original BSD implementation of the public wire layout.
 * Reference: Sky1-Linux/linux-sky1, CIX fw_v3/mve_protocol_def.h,
 * commit 57e018a398248d7e5e4d798610df79a557c0629f (maintained 6.18 LTS).
 * Checksum framing also references if/mvx_firmware_v2.c at that revision.
 * No Linux implementation or comments are incorporated here.
 *
 * data points to exactly CIXVPU_RING_BYTES of data, without control words.
 * Positions are host-endian word indices, sampled by the caller. Each message
 * has a little-endian 16-bit code, a little-endian 16-bit byte length, then a
 * payload padded to four bytes. One unused word distinguishes full and empty.
 * The caller must serialize each producer and each consumer. It must acquire
 * a coherent snapshot, synchronize bus_dma memory before access, and commit
 * the returned next position only after success. After a write, it must sync
 * the data and issue the required producer barrier before publishing next.
 * After a read, it must issue the required consumer barrier before publishing
 * next. These functions do not access device indices, perform DMA sync, or
 * provide memory barriers. They do not establish working hardware support.
 *
 * All objects must be addressable for their stated sizes. Data and payload
 * may be unaligned. Output scalars must have their natural C alignment. The
 * queue, payload, and scalar output objects must not overlap. A NULL payload
 * is allowed only when its supplied size or capacity is zero.
 *
 * EAGAIN means empty (read) or insufficient free words (write). EPROTO means
 * a committed header describes an oversized or incomplete message, or an
 * enabled receive checksum does not match its snapshot. EINVAL
 * means invalid arguments, positions, sizes, or overlapping objects.
 * EMSGSIZE means read capacity is short: only *length changes, to the exact
 * required byte length. The caller can retry the same snapshot. All other
 * errors leave the queue and every output unchanged. On success, read writes
 * exactly *length payload bytes, code, length, and next. Write returns next
 * and zeroes wire padding. The caller decides whether to commit next.
 */
int cixvpu_ring_read(const void *, size_t, uint16_t, uint16_t, uint16_t *,
    void *, size_t, size_t *, uint16_t *);
int cixvpu_ring_write(void *, size_t, uint16_t, uint16_t, uint16_t,
    const void *, size_t, uint16_t *);

/*
 * The selected r0p0-sum0005 HEVC firmware needs write_sum on all three host
 * queues. It adds one LE32 cumulative checksum after the header and before
 * the payload. This word is not included in header.size or the running sum.
 * sum advances by the header word plus zero-padded payload words, modulo
 * 2^32. reserved shifts left and receives the new sum in reserved[2].
 *
 * Firmware-to-host message framing remains header plus payload. read_sum is
 * for the receive MESSAGE queue only, not the two receive BUFFER queues.
 * Its input sum covers previously consumed words. Its reserved values must
 * be sampled coherently with write. The input sum plus ALL committed words
 * must match at least one reserved value. On success only the consumed
 * message words advance sum; reserved stays as sampled. This explicit API
 * keeps checksum validation enabled even when counters wrap to zero. Use
 * plain read for receive buffer queues. It does not perform this check.
 *
 * Both checksum APIs require distinct, nonoverlapping input and output
 * checksum objects. Values are host-endian. All error rules above apply to
 * next_checksum too: it changes only on success. Commit next_checksum and
 * next together after the caller accepts the result. These functions do not
 * access the shared control area. No hardware acceptance is implied.
 */
int cixvpu_ring_read_sum(const void *, size_t, uint16_t, uint16_t,
    const struct cixvpu_ring_checksum *, uint16_t *, void *, size_t, size_t *,
    uint16_t *, struct cixvpu_ring_checksum *);
int cixvpu_ring_write_sum(void *, size_t, uint16_t, uint16_t, uint16_t,
    const void *, size_t, const struct cixvpu_ring_checksum *, uint16_t *,
    struct cixvpu_ring_checksum *);

#endif /* _DEV_IC_CIXVPU_RING_H_ */
