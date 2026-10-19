#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD regression for the native SPI configuration error path.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
source=${1:-"$root/sys/dev/spi/spi.c"}
[ -f "$source" ] || exit 1
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-spi-error.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/test.c" <<'EOF'
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define SPI_F_DONE 1
#define SPI_F_ERROR 2
#define SPIC_BUSY 1
#define IPL_VM 1
#define MUTEX_DEFAULT 0
#define __unused __attribute__((unused))
typedef void *device_t;
typedef int kmutex_t;
typedef int kcondvar_t;
struct spi_chunk {
	struct spi_chunk *chunk_next;
	int chunk_count, chunk_rresid, chunk_wresid;
	uint8_t *chunk_read, *chunk_rptr;
	const uint8_t *chunk_write, *chunk_wptr;
};
struct spi_transfer {
	struct spi_chunk *st_chunks;
	int st_flags, st_errno, st_slave;
	void *st_private, *st_spiprivate;
	void (*st_done)(struct spi_transfer *);
	kmutex_t st_lock;
	kcondvar_t st_cv;
};
struct spi_controller {
	void *sct_cookie;
	int (*sct_configure)(void *, int, int, int);
	int (*sct_transfer)(void *, struct spi_transfer *);
};
struct spi_softc {
	int sc_flags, sc_mode, sc_speed, sc_slave;
	kmutex_t sc_lock;
	kcondvar_t sc_cv;
};
struct spi_handle {
	struct spi_softc *sh_sc;
	struct spi_controller *sh_controller;
	int sh_mode, sh_speed, sh_slave;
};
static unsigned int waits, broadcasts, configurations, transfers;
static bool reject_config, reject_transfer;
static void mutex_init(int *p, int x, int y) { (void)x; (void)y; *p = 0; }
static void mutex_destroy(int *p) { assert(*p == 0); }
static void mutex_enter(int *p) { assert(*p == 0); *p = 1; }
static void mutex_exit(int *p) { assert(*p == 1); *p = 0; }
static void cv_init(int *p, const char *s) { (void)s; *p = 0; }
static void cv_destroy(int *p) { (void)p; }
static void cv_wait(int *p, int *m)
{
	(void)p; (void)m; ++waits;
	/* A completion must already exist for either synchronous backend path. */
	assert(!"SPI configuration error would sleep forever");
}
static void cv_broadcast(int *p) { (void)p; ++broadcasts; }
void spi_done(struct spi_transfer *, int);
static int configure(void *p, int slave, int mode, int speed)
{
	(void)p; (void)slave; (void)mode; (void)speed; ++configurations;
	return reject_config ? EINVAL : 0;
}
static int transfer(void *p, struct spi_transfer *st)
{
	(void)p; ++transfers;
	assert(!(st->st_flags & SPI_F_DONE));
	spi_done(st, reject_transfer ? EIO : 0);
	/* arspi is an existing backend that completes and returns the error. */
	return reject_transfer ? EIO : 0;
}
EOF
extract()
{
	awk -v name="$1" -v type="$2" '
		$0 ~ ("^" name "\\(") { print type; copying = 1 }
		copying { print }
		copying && /^}/ { exit }
	' "$source" >> "$work/test.c"
}
extract spi_acquire 'static void'
extract spi_release 'static void'
extract spi_configure int
extract spi_transfer_init void
extract spi_chunk_init void
extract spi_transfer_add void
extract spi_transfer int
extract spi_wait void
extract spi_done void
extract spi_send_recv int
cat >> "$work/test.c" <<'EOF'
int main(void)
{
	struct spi_softc bus = { .sc_slave = -1 };
	struct spi_controller controller = {
		.sct_configure = configure, .sct_transfer = transfer
	};
	struct spi_handle handle = {
		.sh_sc = &bus, .sh_controller = &controller, .sh_slave = 0
	};
	uint8_t command[2] = { 0, 0x37 }, answer = 0;
	int error;

	spi_configure(NULL, &handle, 0, 1000000);
	reject_config = true;
	error = spi_send_recv(&handle, 2, command, 1, &answer);
	assert(error == EINVAL && bus.sc_flags == 0);
	assert(configurations == 1 && transfers == 0 && broadcasts == 2);
	assert(bus.sc_slave == -1);

	reject_config = false;
	error = spi_send_recv(&handle, 2, command, 1, &answer);
	assert(error == 0 && bus.sc_flags == 0);
	assert(configurations == 2 && transfers == 1 && broadcasts == 4);

	reject_transfer = true;
	error = spi_send_recv(&handle, 2, command, 1, &answer);
	assert(error == EIO && bus.sc_flags == 0);
	assert(configurations == 2 && transfers == 2 && broadcasts == 6);
	assert(waits == 0);
	puts("PASS SPI configure failure completes once, releases bus, retries; backend error completes once.");
	return 0;
}
EOF
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "$work/test.c" -o "$work/test"
"$work/test"
