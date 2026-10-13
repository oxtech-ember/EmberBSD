#!/bin/sh
# A733 NPU staged identification: pure decisions and sysctl dry run.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-npu.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '
/^sun60i_npu_run_stage\(/ {copy=1; print "static void"}
copy {print} copy && /^}/ {exit}
' "$src/sys/arch/arm/sunxi/sun60i_a733_npu.c" > "$work/npu.h"
cat > "$work/check.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
typedef unsigned int u_int;
#define mutex_enter(m) do { } while (0)
#define mutex_exit(m) do { } while (0)
struct sun60i_npu_softc {
	u_int sc_run_stage, sc_run_result;
	bool sc_run_pass, sc_have_id;
	uint64_t sc_pid, sc_rev;
};
static struct sun60i_npu_softc sc;
static int power_node_ok = 1;
static int next_domain_error, next_clocks_error, next_ids_error, next_power_error;
static int sun60i_npu_power_node(struct sun60i_npu_softc *s) { return power_node_ok ? 2 : -1; }
static int of_compatible_match(int node, const void *c) { return 1; }
static int sun60i_npu_stage_domain(struct sun60i_npu_softc *s, int p) { return next_domain_error; }
static int sun60i_npu_stage_clocks(struct sun60i_npu_softc *s) { return next_clocks_error; }
static int sun60i_npu_read_ids(struct sun60i_npu_softc *s) { return next_ids_error; }
static int sun60i_npu_stage_power_id(struct sun60i_npu_softc *s) { return next_power_error; }
#define A733_NPU_STAGE_DOMAIN 1
#define A733_NPU_STAGE_CLOCKS 2
#define A733_NPU_STAGE_RAW_ID 3
#define A733_NPU_STAGE_POWER_ID 4
static const int power_compat[1];
#include "npu.h"
int
main(void)
{
	unsigned checks = 0;

	sc.sc_run_pass = true;
	sun60i_npu_run_stage(&sc, 0);
	assert(sc.sc_run_result == 0 && sc.sc_run_stage == 0);
	sun60i_npu_run_stage(&sc, 2);
	assert(sc.sc_run_result == EINVAL && sc.sc_run_stage == 0);
	checks += 2;

	next_domain_error = 0;
	sun60i_npu_run_stage(&sc, 1);
	assert(sc.sc_run_stage == 1 && sc.sc_run_result == 0);
	next_clocks_error = 0;
	sun60i_npu_run_stage(&sc, 2);
	assert(sc.sc_run_stage == 2 && sc.sc_run_result == 0);
	next_ids_error = EFAULT;
	sun60i_npu_run_stage(&sc, 3);
	assert(sc.sc_run_result == EFAULT && !sc.sc_run_pass);
	checks += 3;
	sun60i_npu_run_stage(&sc, 4);
	assert(sc.sc_run_result == EINVAL);
	checks++;

	memset(&sc, 0, sizeof(sc)); sc.sc_run_pass = true;
	next_domain_error = EBUSY;
	sun60i_npu_run_stage(&sc, 1);
	assert(sc.sc_run_stage == 1 && sc.sc_run_pass);
	next_clocks_error = ENXIO;
	sun60i_npu_run_stage(&sc, 2);
	assert(sc.sc_run_result == ENXIO && !sc.sc_run_pass);
	checks += 2;

	memset(&sc, 0, sizeof(sc)); sc.sc_run_pass = true;
	power_node_ok = 0;
	sun60i_npu_run_stage(&sc, 1);
	assert(sc.sc_run_result == EINVAL && sc.sc_run_stage == 0);
	checks++;

	printf("PASS: %u NPU staged identification checks\n", checks);
	return 0;
}
C
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${A733_NPU_CFLAGS:-} -I"$work" "$work/check.c" -o "$work/check"
"$work/check"
