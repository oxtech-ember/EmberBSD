#!/bin/sh
# Run the actual command transport with bounded synthetic firmware replies.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aicwf-command.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^aicwf_cmd_reply\(/ {copy=1; print "static int"}
copy {print} copy && /^}/ {exit}' \
    "$src/sys/dev/sdmmc/if_aicwf.c" > "$work/command.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define KASSERT assert
#define AICWF_MSG_MAX_PARAM 1032
#define AICWF_TX_MSG_OFFSET 8
#define AICWF_MSG_HDR_LEN 8
#define AICWF_HDR_LEN 4
#define AICWF_TYPE_MSG 0x11
#define AICWF_TASK(id) ((id) >> 10)
#define AICWF_TASK_DRIVER 100
#define AICWF_CMD_TIMEOUT_MS 2000
#define AICWF_SCANU_START_REQ 0x1000
#define AICWF_SCANU_CANCEL_REQ 0x100a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define explicit_memset memset
struct aicwf_softc {
    uint8_t sc_txbuf[2048], sc_cfm[1032];
    int sc_lock, sc_cv; uint64_t sc_sae_epoch;
    bool sc_cmd_busy, sc_cfm_done;
    bool sc_sta_scan_pending, sc_sta_scan_started, sc_sta_scan_draining;
    bool sc_scanning, sc_scan_published;
    uint16_t sc_cfm_len, sc_cfm_id;
};
static bool aicwf_sta_ready(struct aicwf_softc *sc) { (void)sc; return true; }
static int write_error, reply_len, writes; static struct aicwf_softc *waiting;
static int mutex_owned(int *p) { return *p; }
static void mutex_enter(int *p) { assert(!*p); *p = 1; }
static void mutex_exit(int *p) { assert(*p); *p = 0; }
static void cv_wait(int *cv, int *lock) { (void)cv; assert(*lock); assert(waiting); waiting->sc_sae_epoch++; waiting->sc_cmd_busy = false; }
static int cv_timedwait(int *cv, int *lock, int ticks)
{ (void)cv; (void)lock; assert(ticks == 2000); return ETIMEDOUT; }
static void cv_broadcast(int *cv) { (void)cv; }
static int mstohz(int n) { return n; }
static void le16enc(void *p, unsigned n)
{ uint8_t *b = p; b[0] = n; b[1] = n >> 8; }
static uint8_t aicwf_crc8(const void *p, size_t n)
{ (void)p; (void)n; return 0; }
static int aicwf_write_attempted(struct aicwf_softc *sc, size_t len, unsigned reserve, bool *attempted)
{
    assert(mutex_owned(&sc->sc_lock));
    if (attempted != NULL) *attempted = true;
    assert(len == 20 && reserve == 0); writes++;
    sc->sc_cfm_done = reply_len >= 0;
    sc->sc_cfm_len = reply_len >= 0 ? reply_len : 0;
    memset(sc->sc_cfm, 0, sizeof(sc->sc_cfm));
    sc->sc_cfm[1] = 7;
    return write_error;
}
#include "command.h"
static void check(bool strict, int len, int transport_error, int expected)
{
    struct aicwf_softc sc = {0};
    uint8_t reply[4] = {0xff, 0xff, 0xff, 0xff};
    const uint8_t secret[4] = {1, 2, 3, 4};
    reply_len = len;
    write_error = transport_error;
    assert(aicwf_cmd_reply(&sc, 0x24, secret, 4, 0x25,
        reply, sizeof(reply), strict, 0) == expected);
    assert(!sc.sc_cmd_busy && sc.sc_cfm_id == 0 && sc.sc_lock == 0);
    if (strict)
        assert(memcmp(sc.sc_txbuf + 16, "\0\0\0\0", 4) == 0);
    if (expected)
        assert(reply[0] == 0xff && reply[1] == 0xff);
    else
        assert(reply[0] == 0 && reply[1] == (len >= 2 ? 7 : 0));
}
int main(void)
{
    check(true, 0, 0, EPROTO);
    check(true, 1, 0, EPROTO);
    check(true, 3, 0, EPROTO);
    check(true, 4, 0, 0);
    check(true, 8, 0, 0);
    check(true, -1, 0, ETIMEDOUT);
    check(true, 4, EIO, EIO);
    check(false, 1, 0, 0);
    struct aicwf_softc sc = {.sc_sae_epoch = 17, .sc_cmd_busy = true};
    const uint8_t param[4] = {0};
    waiting = &sc;
    writes = 0;
    assert(aicwf_cmd_reply(&sc, 0x24, param, 4, 0x25,
        NULL, 0, true, 17) == ESTALE);
    assert(writes == 0 && sc.sc_lock == 0 && !sc.sc_cmd_busy);
    reply_len = 0;
    assert(aicwf_cmd_reply(&sc, 0x24, param, 4, 0x25,
        NULL, 0, true, 18) == 0);
    assert(writes == 1 && sc.sc_lock == 0 && !sc.sc_cmd_busy);
    puts("PASS: 10 command reply, lifetime, cleanup and erasure cases");

    return 0;
}
C
${CC:-cc} -std=c99 -Wall -Wextra -Werror ${AICWF_COMMAND_CFLAGS:-} "$work/check.c" -o "$work/check"
"$work/check"
