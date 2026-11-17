/* Origin: EmberBSD (AI-assisted). */
/*
 * Run actual sdmmc single-block splitting, write completion and ld retry code.
 * The model replaces host commands and kernel scheduling, not these decisions.
 */
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __BIT
#define __BIT(n) (1U << (n))
#endif
#define DEV_BSIZE 512
#define DEV_BSHIFT 9
#define ISSET(a, b) ((a) & (b))
#define KASSERT(c) assert(c)
#define DPRINTF(...) ((void)0)
#define SDMMC_LOCK(sc) ((void)(sc))
#define SDMMC_UNLOCK(sc) ((void)(sc))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define howmany(a, b) (((a) + (b) - 1) / (b))
#define aprint_error_dev(...) ((void)0)
#define LOG_PRINTF 0
#include "sdmmc-write-flags.inc"

typedef unsigned char u_char;
typedef unsigned int u_int;
typedef void *bus_dmamap_t;
struct sdmmc_command {
	uint16_t c_opcode;
	uint32_t c_arg, c_resp[4];
	void *c_data;
	int c_datalen, c_blklen, c_flags, c_error;
	bus_dmamap_t c_dmamap;
};
struct counter { uint64_t ev_count; };
struct sdmmc_softc {
	unsigned sc_flags, sc_caps;
	bus_dmamap_t sc_dmap;
	struct counter sc_ev_xfer, sc_ev_xfer_error, sc_ev_xfer_aligned[8],
	    sc_ev_xfer_unaligned;
};
struct sdmmc_function {
	struct sdmmc_softc *sc;
	unsigned flags, rca;
	struct { unsigned capacity; } csd;
};
struct buf {
	unsigned b_flags;
	uint64_t b_rawblkno;
	int b_bcount, b_error, b_resid;
	u_char *b_data;
};
struct dk_softc {
	void *sc_dev;
	struct { void *dk_label; } sc_dkdev;
};
struct ld_softc { void *sc_dv; struct dk_softc sc_dksc; };
struct ld_sdmmc_softc {
	struct ld_softc sc_ld;
	struct sdmmc_function *sc_sf;
	int sc_lock;
	bool sc_dying;
};
struct ld_sdmmc_task {
	struct ld_sdmmc_softc *task_sc;
	struct buf *task_bp;
	int task_retries, task_restart_ch;
};
struct cfdriver { const char *cd_name; };
static int hz = 1000;
static unsigned writes, statuses, reads, releases, completions;
static unsigned write_fail_at, status_error_at;
static uint32_t trace[32];
static bool scheduled, owned, status_never_ready;
static unsigned mock_ticks, pauses, delays;
static bool frozen_clock;
static const char *panicstr;
static int doing_shutdown;
static int getticks(void) { return (int)mock_ticks; }
static int kpause(const char *name, bool intr, int ticks, void *lock) {
	(void)name; (void)intr; (void)lock;
	assert(ticks > 0);
	pauses++;
	assert(panicstr == NULL && !doing_shutdown);
	if (!frozen_clock)
		mock_ticks += ticks;
	return 0;
}

static void sdmmc_pause(u_int usecs, void *lock) {
	assert(usecs == 10000);
	(void)kpause("sdmmcdelay", false, hz / 100, lock);
}
static void sdmmc_delay(u_int usecs) {
	assert(usecs == 10000);
	delays++;
}

static int sdmmc_select_card(struct sdmmc_softc *sc, struct sdmmc_function *sf) {
	(void)sc; (void)sf;
	return 0;
}
static int
sdmmc_mmc_command(struct sdmmc_softc *sc, struct sdmmc_command *command)
{
	(void)sc;
	if (command->c_opcode == MMC_WRITE_BLOCK_SINGLE) {
		assert(command->c_datalen == 512 && command->c_blklen == 512);
		trace[writes++] = command->c_arg;
		return writes == write_fail_at ? ETIMEDOUT : 0;
	}
	assert(command->c_opcode == MMC_SEND_STATUS);
	statuses++;
	assert(statuses < 6000); /* Fail the formerly unbounded polling path. */
	command->c_resp[0] = status_never_ready ? 0 : MMC_R1_READY_FOR_DATA;
	if (statuses == status_error_at)
		command->c_resp[0] |= 0x04000000; /* delayed WP failure with READY */
	return 0;
}
static int sdmmc_app_command(struct sdmmc_softc *sc, struct sdmmc_function *sf,
    struct sdmmc_command *cmd) {
	(void)sc; (void)sf; (void)cmd;
	assert(false); /* SINGLE_ONLY must not send multi-block setup. */
	return EIO;
}

#include "sdmmc-write-subr.inc"
#include "sdmmc-write-single.inc"

/* Model only the outer dispatch to the real SINGLE_ONLY implementation. */
static int sdmmc_mem_write_block(struct sdmmc_function *sf, uint32_t block,
    u_char *data, size_t length) {
	assert(sf->sc->sc_caps & SMC_CAPS_SINGLE_ONLY);
	return sdmmc_mem_single_write_block(sf, block, data, length);
}
static int sdmmc_mem_read_block(struct sdmmc_function *sf, uint32_t block,
    u_char *data, size_t length) {
	(void)sf; (void)block; (void)data; (void)length;
	reads++;
	return EIO;
}
static void mutex_enter(int *lock) { (void)lock; assert(!owned); owned = true; }
static void mutex_exit(int *lock) { (void)lock; assert(owned); owned = false; }
static struct cfdriver *device_cfdriver(void *dev) {
	(void)dev;
	static struct cfdriver driver = {"ld"};
	return &driver;
}
static void diskerr(struct buf *bp, const char *name, const char *what, int log,
    int part, void *label) {
	(void)bp; (void)name; (void)what; (void)log; (void)part; (void)label;
}
static void ld_sdmmc_restart(void *task) { (void)task; }
static void callout_reset(int *callout, int delay, void (*function)(void *),
    void *arg) {
	(void)callout; (void)delay; (void)function; (void)arg;
	assert(owned);
	scheduled = true;
}
static void ld_sdmmc_task_put(struct ld_sdmmc_softc *sc, struct ld_sdmmc_task *t) {
	(void)sc; (void)t;
	assert(owned);
	releases++;
}
static void lddone(struct ld_softc *ld, struct buf *bp) {
	(void)ld; (void)bp;
	assert(!owned);
	completions++;
}
#include "sdmmc-write-ld.inc"

static void
run_bio(bool no_retry, bool read, unsigned fail_write, unsigned fail_status,
    unsigned expected_writes, unsigned expected_reads, int expected_error)
{
	uint8_t data[1024] = {0};
	struct sdmmc_softc bus = {.sc_flags = SMF_SD_MODE,
	    .sc_caps = SMC_CAPS_SINGLE_ONLY | (no_retry ? SMC_CAPS_NO_WRITE_RETRY : 0)};
	struct sdmmc_function card = {.sc = &bus, .flags = SFF_SDHC, .rca = 1,
	    .csd.capacity = 100};
	struct ld_sdmmc_softc ld = {.sc_sf = &card};
	struct buf bp = {.b_rawblkno = 10, .b_bcount = sizeof(data), .b_data = data,
	    .b_flags = read ? B_READ : 0};
	struct ld_sdmmc_task task = {.task_sc = &ld, .task_bp = &bp};
	writes = statuses = reads = releases = completions = 0;
	pauses = delays = 0;
	mock_ticks = UINT32_MAX - 2000; /* Exercise elapsed-time wraparound. */
	write_fail_at = fail_write;
	status_error_at = fail_status;
	unsigned attempts = 0;
	do {
		scheduled = false;
		ld_sdmmc_dobio(&task);
		attempts++;
		assert(attempts < 10);
	} while (scheduled);
	assert(writes == expected_writes && reads == expected_reads);
	assert(bp.b_error == expected_error);
	assert(bp.b_resid == (expected_error ? 1024 : 0));
	assert(releases == 1 && completions == 1);
	if (writes >= 2)
		assert(trace[0] == 10 && trace[1] == 11);
	if (writes == 4)
		assert(trace[2] == 10 && trace[3] == 11);
}

int
main(int argc, char **argv)
{
	assert(argc == 2);
	if (strcmp(argv[1], "retry") == 0) {
		run_bio(true, false, 2, 0, 2, 0, ETIMEDOUT);
		run_bio(false, false, 2, 0, 4, 0, 0);
		run_bio(true, true, 0, 0, 0, 6, EIO);
	} else if (strcmp(argv[1], "timeout") == 0) {
		status_never_ready = true;
		run_bio(true, false, 0, 0, 1, 0, ETIMEDOUT);
		assert(statuses == 501 && pauses == 500 && delays == 0);
		frozen_clock = true;
		run_bio(true, false, 0, 0, 1, 0, ETIMEDOUT);
		assert(statuses == 501 && pauses == 500 && delays == 0);
		panicstr = "test panic";
		run_bio(true, false, 0, 0, 1, 0, ETIMEDOUT);
		assert(statuses == 501 && delays == 500 && pauses == 0);
		panicstr = NULL;
		doing_shutdown = 1;
		run_bio(true, false, 0, 0, 1, 0, ETIMEDOUT);
		assert(statuses == 501 && delays == 500 && pauses == 0);
	} else {
		assert(strcmp(argv[1], "status") == 0);
		run_bio(true, false, 0, 2, 2, 0, EIO);
	}
	puts("sdmmc write contract: PASS");
	return 0;
}
