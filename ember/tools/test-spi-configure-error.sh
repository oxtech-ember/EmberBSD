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
#define __BIT(n) (1U << (n))
#define SPI_F_DONE 1
#define SPI_F_ERROR 2
#define SPIC_BUSY 1
#define IPL_VM 1
#define MUTEX_DEFAULT 0
#ifndef __unused
#define __unused __attribute__((unused))
#endif
typedef void *device_t;
typedef int kmutex_t;
typedef int kcondvar_t;
typedef unsigned int u_int;
struct spi_chunk {
	struct spi_chunk *chunk_next;
	int chunk_count, chunk_rresid, chunk_wresid;
	uint8_t *chunk_read, *chunk_rptr;
	const uint8_t *chunk_write, *chunk_wptr;
	u_int chunk_bus_width;
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
	u_int sct_caps;
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
# Use capability definitions from the real public header.
awk '/^#define[[:space:]]+SPI_CAP_/ { print }' \
    "$(dirname -- "$source")/spivar.h" >> "$work/test.c"
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
static void
check_width(struct spi_handle *handle, unsigned int width, unsigned int caps,
    bool write, bool read, int expected)
{
	struct spi_transfer st;
	struct spi_chunk header, data;
	uint8_t command = 0x32, value = 0;
	unsigned int before = transfers;
	unsigned int configured = configurations;

	handle->sh_controller->sct_caps = caps;
	/* Force reconfiguration if validation incorrectly lets this through. */
	handle->sh_sc->sc_slave = -1;
	spi_transfer_init(&st);
	spi_chunk_init(&header, 1, &command, NULL);
	spi_chunk_init(&data, 1, write ? &value : NULL, read ? &value : NULL);
	data.chunk_bus_width = width;
	spi_transfer_add(&st, &header);
	spi_transfer_add(&st, &data);
	assert(spi_transfer(handle, &st) == 0);
	spi_wait(&st);
	assert(st.st_errno == expected);
	assert(transfers == before + (expected == 0));
	assert(configurations == configured + (expected == 0));
	assert(handle->sh_sc->sc_flags == 0);
}

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
	reject_transfer = false;
	check_width(&handle, 0, 0, true, true, 0);
	check_width(&handle, 1, 0, true, true, 0);
	check_width(&handle, 2, 0, true, false, EOPNOTSUPP);
	check_width(&handle, 2, SPI_CAP_DUAL_TX, true, false, 0);
	check_width(&handle, 2, SPI_CAP_DUAL_TX, false, true, EOPNOTSUPP);
	check_width(&handle, 2, SPI_CAP_DUAL_RX, false, true, 0);
	check_width(&handle, 4, 0, true, false, EOPNOTSUPP);
	check_width(&handle, 4, SPI_CAP_QUAD_TX, true, false, 0);
	check_width(&handle, 4, SPI_CAP_QUAD_TX, false, true, EOPNOTSUPP);
	check_width(&handle, 4, SPI_CAP_QUAD_RX, false, true, 0);
	check_width(&handle, 3, ~0U, true, false, EINVAL);
	check_width(&handle, 4, ~0U, true, true, EINVAL);
	check_width(&handle, 4, ~0U, false, false, EINVAL);
	check_width(&handle, 1, 0, true, false, 0);
	assert(waits == 0);
	puts("PASS SPI completion, retry, legacy transfers and directional dual/quad phases.");
	return 0;
}
EOF
# CFLAGS follows the usual shell-separated compiler flag convention.
"${CC:-cc}" ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror "$work/test.c" -o "$work/test"
"$work/test"
