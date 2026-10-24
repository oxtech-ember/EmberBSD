/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD kernel-only finite ADI AXI DMAC API. */
/* Kernel-only ADI AXI DMAC API. Addresses never originate from userspace. */
#ifndef _DEV_FDT_ADI_AXI_DMACVAR_H_
#define _DEV_FDT_ADI_AXI_DMACVAR_H_
#include <sys/types.h>
#include <sys/device.h>

struct adi_axi_dmac_channel;
struct adi_axi_dmac_buffer;
struct adi_axi_dmac_guard;

struct adi_axi_dmac_drain_ops {
	/* CONTROL is disabled before this callback. Success must prove that all
	 * accepted bus transactions completed. Never re-enter this channel.
	 */
	int (*drain)(void *, unsigned int timeout_ms);
};

int adi_axi_dmac_acquire(int phandle, device_t,
	const struct adi_axi_dmac_drain_ops *, void *, struct adi_axi_dmac_channel **);
int adi_axi_dmac_release(struct adi_axi_dmac_channel *);
int adi_axi_dmac_stop(struct adi_axi_dmac_channel *, unsigned int timeout_ms);
int adi_axi_dmac_buffer_alloc(struct adi_axi_dmac_channel *, size_t,
	struct adi_axi_dmac_buffer **);
int adi_axi_dmac_buffer_free(struct adi_axi_dmac_buffer *);
/*
 * Kernel buffers only. CPU access to an inactive buffer may run during a
 * transfer on another buffer. Access to the active buffer returns EBUSY
 * until the transfer and its checked drain finish.
 *
 * The owner must keep handles alive for every call. Serialize buffer_free
 * and channel release with all calls that use the handle being destroyed.
 * Stop serializes with transfers; it does not cancel a transfer in progress.
 */
int adi_axi_dmac_buffer_read(struct adi_axi_dmac_buffer *, void *, size_t);
int adi_axi_dmac_buffer_write(struct adi_axi_dmac_buffer *, const void *, size_t);
/*
 * Optional TX-only cache preparation for an inactive, valid range.
 * PREWRITE transfers CPU ownership before the later hardware submission.
 * The same length is idempotent. A valid length change closes the previous
 * pair with POSTWRITE before a new PREWRITE. Invalid calls keep preparation.
 *
 * Read/write/free cancel unused preparation with POSTWRITE before CPU access
 * or unmapping. Transfer of the prepared length consumes it without PREWRITE;
 * other lengths use the ordinary sync path. Submitted ranges receive
 * POSTWRITE only after a positive drain. Quarantine retains all ownership.
 *
 * Preparation may overlap a transfer on another buffer. Its completion drain
 * preserves unused preparation. Explicit stop and shutdown cancel preparation
 * after a successful drain. A prepare concurrent with stop may finish after
 * stop; the owner must quiesce preparation when it needs all buffers canceled.
 * Handle lifetime rules are the same as for buffer read/write.
 */
int adi_axi_dmac_buffer_prepare(struct adi_axi_dmac_buffer *, size_t);

/* One finite transfer, native IRQ completion, bounded synchronous wait.
 * Stream/FIFO direction is fixed by hardware; cyclic/2D are not supported.
 * Memory-to-AXIS emits TLAST on the final beat. Memory-to-FIFO does not
 * need a packet marker. AXIS-to-memory is refused without partial reporting.
 * Successful DMA completion does not prove that a downstream DAC emitted
 * the samples. The consumer must check its own output-completion state.
 */
int adi_axi_dmac_transfer(struct adi_axi_dmac_buffer *, size_t,
	unsigned int timeout_ms);

/* Exclude DMA consumers while a converter capture lease owns the pipeline.
 * A zero CONTROL is admission only, not evidence of drained AXI. The caller
 * must separately obtain the platform's positive drain acknowledgement.
 */
int adi_axi_dmac_reserve(int phandle, device_t, struct adi_axi_dmac_guard **);
int adi_axi_dmac_guard_check(struct adi_axi_dmac_guard *);
/* Reject any known channel ownership since attach, pending/enabled state,
 * or shutdown. This check still requires a trusted platform cold handoff;
 * it does not turn disabled control bits into an AXI drain certificate.
 */
int adi_axi_dmac_guard_cold_check(struct adi_axi_dmac_guard *);
/* Positive hardware witness for exact 4.5.c without SG. No register writes,
 * ownership changes, buffer publication or repeated sc_lock acquisition.
 * The converter owner must keep this guard alive and exclude new submissions
 * for the entire poll. Required source/destination clocks must stay running.
 * Older/unknown layouts return EOPNOTSUPP; missing debug status times out.
 */
int adi_axi_dmac_guard_wait_idle(const struct adi_axi_dmac_guard *,
	unsigned int timeout_ms);
/* Bind a private consumer and prepare finite FIFO-to-memory capture. */
int adi_axi_dmac_guard_bind(struct adi_axi_dmac_guard *,
	const struct adi_axi_dmac_drain_ops *, void *);
int adi_axi_dmac_guard_prepare(struct adi_axi_dmac_guard *);
int adi_axi_dmac_guard_buffer_alloc(const struct adi_axi_dmac_guard *, size_t,
	struct adi_axi_dmac_buffer **);
int adi_axi_dmac_guard_buffer_read(const struct adi_axi_dmac_guard *,
	struct adi_axi_dmac_buffer *, void *, size_t);
int adi_axi_dmac_guard_transfer(const struct adi_axi_dmac_guard *,
	struct adi_axi_dmac_buffer *, size_t, unsigned int timeout_ms);
int adi_axi_dmac_guard_buffer_free(const struct adi_axi_dmac_guard *,
	struct adi_axi_dmac_buffer *);
int adi_axi_dmac_unreserve(struct adi_axi_dmac_guard *);

#endif

/* RX FIFO queue. The caller serializes session lifetime and buffer release.
 * next returns a synchronized, CPU-owned buffer. Re-submit returns ownership.
 * Stop must succeed before freeing any mapping. No cyclic or SG DMA is used.
 */
struct adi_axi_dmac_stream_status {
 uint64_t sequence;
 uint64_t completed_blocks;
 uint64_t completed_bytes;
 uint64_t discontinuities;
 uint32_t queued;
 uint32_t ready;
};
int adi_axi_dmac_stream_start(struct adi_axi_dmac_channel *);
int adi_axi_dmac_stream_submit(struct adi_axi_dmac_buffer *, size_t);
int adi_axi_dmac_stream_next(struct adi_axi_dmac_channel *,
 struct adi_axi_dmac_buffer **, struct adi_axi_dmac_stream_status *);
int adi_axi_dmac_guard_stream_start(const struct adi_axi_dmac_guard *);
int adi_axi_dmac_guard_stream_submit(const struct adi_axi_dmac_guard *,
 struct adi_axi_dmac_buffer *, size_t);
int adi_axi_dmac_guard_stream_next(const struct adi_axi_dmac_guard *,
 struct adi_axi_dmac_buffer **, struct adi_axi_dmac_stream_status *);
int adi_axi_dmac_guard_stream_stop(const struct adi_axi_dmac_guard *, unsigned);

/* The notify callback runs at IPL_BIO and must not sleep or take driver locks. */
int adi_axi_dmac_guard_stream_notify(const struct adi_axi_dmac_guard *,
 void (*)(void *), void *);
