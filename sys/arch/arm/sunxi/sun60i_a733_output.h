/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent A733 output timing contract. */
#ifndef _SUN60I_A733_OUTPUT_H
#define _SUN60I_A733_OUTPUT_H

/* Progressive RGB888, one pixel per clock. All timing counts are pixels/lines. */
struct sun60i_a733_output_mode {
	uint32_t pixel_clock_hz;
	uint32_t hdisplay, hsync_start, hsync_end, htotal;
	uint32_t vdisplay, vsync_start, vsync_end, vtotal;
	bool hsync_positive, vsync_positive;
};

struct sun60i_a733_tcon_timing {
	/* TV0 offsets: 0x98, 0x9c, 0xa0, 0xa4, 0xa8 and 0x88. */
	uint32_t basic1, basic2, basic3, basic4, basic5, polarity;
};

/*
 * These functions perform no I/O. A timing is not a clock, PHY, EDID or owner
 * admission. Native prepare/start must use the same coordinator owner cookie.
 * Failure leaves the output unchanged. The caller serializes interrupt state.
 */
int sun60i_a733_tcon_timing_encode(const struct sun60i_a733_output_mode *,
    struct sun60i_a733_tcon_timing *);
/* Return ENOENT unless an enabled vblank is present in the supplied snapshot. */
int sun60i_a733_tcon_vblank_ack(uint32_t, uint32_t *);

/* Fixed cache layout. No word grants readiness or permits a hardware query. */
enum {
	A733_OUTPUT_PREPARE_NONE, A733_OUTPUT_PREPARE_READY,
	A733_OUTPUT_PREPARE_PHY, A733_OUTPUT_PREPARE_TV0_CLOCK,
	A733_OUTPUT_PREPARE_TV0_TIMING, A733_OUTPUT_PREPARE_DW,
	A733_OUTPUT_PREPARE_FINAL_READY, A733_OUTPUT_PREPARE_DONE
};
enum {
	A733_OUTPUT_IO_NONE, A733_OUTPUT_IO_CORE8, A733_OUTPUT_IO_TOP32,
	A733_OUTPUT_IO_TV0, A733_OUTPUT_IO_VO1
};
struct sun60i_a733_output_trace {
	uint32_t version, valid, phase, result, done;
	uint32_t read_space, read_reg, read_value, read_valid;
	uint32_t write_space, write_reg, write_value, write_valid;
};

/*
 * Diagnostic register sample. Word order: VO1 0x00, VO1 0x20, TV0 0x00,
 * TV0 0x04, TV0 0x40, TV0 0x90, TV0 0xfc, TV0 0xfc after 1 ms.
 * Valid bit N means that word N was read.
 */
enum { A733_OUTPUT_SAMPLE_WORDS = 8 };
struct sun60i_a733_output_sample {
	uint32_t version, valid;
	uint32_t words[A733_OUTPUT_SAMPLE_WORDS];
};

/* Cached first failed vblank observation. No MMIO is done by the reader. */
enum sun60i_a733_observe_phase {
	A733_OBSERVE_READY = 1,
	A733_OBSERVE_PHY = 2,
	A733_OBSERVE_TV0 = 3
};
struct sun60i_a733_output_observe_trace {
	uint32_t version, valid, phase, result, sample_error;
	struct sun60i_a733_output_sample sample;
};

/*
 * Native provider. All callbacks run under one sleep mutex. They must not
 * call these dispatch functions. Provider storage and ops have kernel lifetime.
 * The provider obtains CCU/PCK leases with this owner before any MMIO write.
 * get_mode may run DDC. It must not start scanout. prepare must check HPD again.
 * get_mode may return EAGAIN only for checked HPD-low before scanout.
 * Other failures are retained. There is no detach or retry after a fault.
 */
struct sun60i_a733_hdmi_phy_trace;
/*
 * Vblank interrupt callback. It runs in interrupt context with the provider
 * spin lock held, once per counted vblank. It must not sleep or call the
 * dispatch functions. The count starts at zero when the output starts.
 */
typedef void (*sun60i_a733_vblank_fn)(void *, uint64_t);
struct sun60i_a733_output_ops {
	int (*get_mode)(void *, const void *, struct sun60i_a733_output_mode *);
	int (*prepare)(void *, const void *, const struct sun60i_a733_output_mode *);
	int (*start)(void *, const void *);
	int (*observed_vblank)(void *, const void *, uint64_t *);
	void (*fault)(void *, const void *, int);
	/* Optional. Copy initialized cache only, including after a sticky fault. */
	void (*cached_trace)(const void *, struct sun60i_a733_output_trace *);
	/* Optional. Copy initialized PHY cache only, without any I/O. */
	void (*cached_phy_trace)(const void *, struct sun60i_a733_hdmi_phy_trace *);
	/* Optional. Register reads only. It must not write or latch a fault. */
	int (*sample)(void *, const void *, struct sun60i_a733_output_sample *);
	/*
	 * Optional. Install or remove (NULL) the vblank callback. EOPNOTSUPP
	 * means that the output has no vblank interrupt. After return, no call
	 * to a removed callback is in progress.
	 */
	int (*set_vblank_handler)(void *, const void *, sun60i_a733_vblank_fn,
	    void *);
	/* Optional read-only HPD query. A disconnect is not a provider fault. */
	int (*detect)(const void *, const void *, bool *);
	/* Optional. Stop output while retaining mode and resource ownership. */
	int (*stop)(void *, const void *);
	/* Optional. Copy the first failed vblank observation without I/O. */
	void (*cached_observe_trace)(const void *,
	    struct sun60i_a733_output_observe_trace *);
};
int sun60i_a733_output_register(int, const struct sun60i_a733_output_ops *, void *);
int sun60i_a733_output_get_mode(int, const void *,
    struct sun60i_a733_output_mode *);
int sun60i_a733_output_prepare(int, const void *,
    const struct sun60i_a733_output_mode *);
int sun60i_a733_output_start(int, const void *);
int sun60i_a733_output_stop(int, const void *);
/* ENOENT means no new vblank. It does not latch a fault. */
int sun60i_a733_output_observed_vblank(int, const void *, uint64_t *);
int sun60i_a733_output_fault(int, const void *, int);
/* No readiness check or I/O. Failure leaves the output unchanged. */
int sun60i_a733_output_cached_trace(int, struct sun60i_a733_output_trace *);
int sun60i_a733_output_cached_observe_trace(int,
    struct sun60i_a733_output_observe_trace *);
int sun60i_a733_output_cached_phy_trace(int,
    struct sun60i_a733_hdmi_phy_trace *);
/* Reads only, after prepare. An error does not latch an output fault. */
int sun60i_a733_output_sample(int, const void *,
    struct sun60i_a733_output_sample *);
/* After start. EOPNOTSUPP means polled vblank observation only. */
int sun60i_a733_output_set_vblank_handler(int, const void *,
    sun60i_a733_vblank_fn, void *);

/* After prepare. No writes or sticky faults; failure leaves out unchanged. */
int sun60i_a733_output_detect(int, const void *, bool *);

#endif
