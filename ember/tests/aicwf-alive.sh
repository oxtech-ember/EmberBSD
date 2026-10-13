#!/bin/sh
# Link-liveness watchdog of the production aicwf driver: decision math,
# miss accounting, single in-flight work, connection-loss routing.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-alive.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '
/^aicwf_alive_silent\(/ {copy=1; print "static bool"}
/^aicwf_alive_lost\(/ {copy=1; print "static void"}
/^aicwf_alive_work\(/ {copy=1; print "static void"}
/^aicwf_alive_tick\(/ {copy=1; print "static void"}
copy {print} copy && /^}/ {copy=0}
' "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/alive.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#define mstohz(ms) ((ms) / 10)
typedef unsigned int u_int;
unsigned int ticks = 100000;
static int enqueued, scheduled, disconnects, states;
static int prints;
struct workqueue { int dummy; };
static struct workqueue thetaskq;
struct callout { int dummy; };
struct work { int dummy; };
struct ieee80211com { int ic_state; };
struct aicwf_softc {
	int sc_lock;
	uint64_t sc_sae_epoch, sc_alive_epoch;
	struct ieee80211com sc_ic;
	void *sc_taskq;
	struct callout sc_alive_ch;
	struct work sc_alive_work;
	volatile u_int sc_alive_rx;
	u_int sc_alive_misses;
	bool sc_alive_queued, sc_connected, sc_if_attached;
};
static struct aicwf_softc sc = { .sc_taskq = &thetaskq };

static void mutex_enter(int *p) { assert(!*p); *p = 1; }
static void mutex_exit(int *p) { assert(*p); *p = 0; }
static void workqueue_enqueue(struct workqueue *q, struct work *w, void *unused) { enqueued++; }
static void callout_schedule(struct callout *c, int t) { scheduled++; assert(t == 200); }
#define device_printf(...) prints++
#define AICWF_ALIVE_PERIOD_MS 2000
#define AICWF_ALIVE_SILENT_MS 10000
#define IEEE80211_S_RUN 4
#define IEEE80211_S_SCAN 1
static int splnet(void) { return 0; }
static void splx(int s) { (void)s; }
static void aicwf_disconnect(struct aicwf_softc *s) { (void)s; disconnects++; }
static void ieee80211_new_state(void *ic, int state, int arg)
{ (void)ic; (void)arg; assert(state == IEEE80211_S_SCAN); states++; }
#include "alive.h"

int
main(void)
{
	unsigned checks = 0;

	/* Decision: only a believed association can count as silent. */
	sc.sc_if_attached = false; sc.sc_connected = true; sc.sc_alive_rx = ticks;
	assert(!aicwf_alive_silent(&sc, ticks + 2000)); checks++;
	sc.sc_if_attached = true; sc.sc_connected = false;
	assert(!aicwf_alive_silent(&sc, ticks + 2000)); checks++;
	sc.sc_connected = true;
	/* Boundary: exactly 10 s is not yet silent; one tick more is. */
	sc.sc_alive_rx = ticks;
	assert(!aicwf_alive_silent(&sc, ticks + 1000)); checks++;
	assert(aicwf_alive_silent(&sc, ticks + 1001)); checks++;
	/* Wraparound of the tick counter keeps a true signed distance. */
	sc.sc_alive_rx = (unsigned)-5;
	assert(!aicwf_alive_silent(&sc, 100)); checks++;
	assert(aicwf_alive_silent(&sc, 2000)); checks++;

	/* Tick: two silent checks enqueue exactly one work item. */
	sc.sc_alive_rx = ticks - 3000;
	sc.sc_alive_misses = 0; enqueued = 0; scheduled = 0;
	aicwf_alive_tick(&sc);
	assert(enqueued == 0 && sc.sc_alive_misses == 1 && scheduled == 1); checks++;
	aicwf_alive_tick(&sc);
	assert(enqueued == 1 && sc.sc_alive_queued && sc.sc_alive_misses == 0); checks++;
	aicwf_alive_tick(&sc);
	assert(enqueued == 1); checks++;
	/* A fresh frame resets the miss count. */
	sc.sc_alive_rx = ticks;
	aicwf_alive_tick(&sc);
	assert(sc.sc_alive_misses == 0 && enqueued == 1); checks++;

	/* The firmware connection-loss indication routes to the same work. */
	enqueued = 0; sc.sc_alive_queued = false;
	aicwf_alive_lost(&sc);
	assert(enqueued == 1 && sc.sc_alive_queued); checks++;
	aicwf_alive_lost(&sc);
	assert(enqueued == 1); checks++;

	/* Work from a former association must not disconnect its replacement. */
	sc.sc_ic.ic_state = IEEE80211_S_RUN;
	sc.sc_alive_epoch = 19; sc.sc_sae_epoch = 20;
	aicwf_alive_work(&sc);
	assert(disconnects == 0 && states == 0 && !sc.sc_alive_queued); checks++;
	aicwf_alive_lost(&sc);
	assert(sc.sc_alive_epoch == 20 && sc.sc_alive_queued); checks++;
	aicwf_alive_work(&sc);
	assert(disconnects == 1 && states == 1 && !sc.sc_alive_queued); checks++;
	printf("PASS: %u link-liveness watchdog checks\n", checks);
	return 0;
}
C
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${AICWF_ALIVE_CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
