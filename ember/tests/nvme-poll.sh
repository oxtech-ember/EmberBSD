#!/bin/sh
# Execute the production polled admin path of nvme(4) with a fake controller:
# the completion is published to the poller last, a completion claimed by the
# interrupt is waited for, and one after a timeout runs nothing.  Then check
# that deliberate faults are caught.  The fake runs on one thread: it cannot
# catch nvme_q_complete() reading the callback after dropping its lock.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/nvme-poll.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
input=${1:-$src/sys/dev/ic/nvme.c}
regs=$src/sys/dev/ic/nvmereg.h
awk '/^struct nvme_cqe \{/ { copy = 1 } copy { print } copy && /^\}/ { exit }' "$regs" > "$work/nvme-poll-cqe.h"
grep -q '^#define NVME_CQE_PHASE' "$work/nvme-poll-cqe.h"
awk '/^struct nvme_poll_state \{/ { copy = 1 } copy { print } copy && /^\};/ { exit }' "$input" > "$work/nvme-poll-state.h"
grep -q '^struct nvme_poll_state {' "$work/nvme-poll-state.h"
awk '/^(nvme_poll|nvme_poll_fill|nvme_poll_done|nvme_q_complete)\(/ { print prev; copy = 1; n++ }
copy { print }
copy && /^\}/ { copy = 0 }
{ prev = $0 }
END { if (n != 4) exit 1 }' "$input" > "$work/nvme-poll.h"
build() {
	${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter \
	    -I"$work" "$src/ember/tools/nvme-poll-contract.c" \
	    ${LDFLAGS:-} -o "$1"
}
build "$work/check"
${TEST_RUNNER:-} "$work/check"
# Each mutation must make the contract fail.
cp "$work/nvme-poll.h" "$work/orig.h"
caught=0
for m in 's/membar_release();/(void)0;/' \
    's/membar_acquire();/(void)0;/' \
    's/(\*done)(q, ccb, &c);/(void)done;/' \
    's/ccb->ccb_cookie = state->cookie;/(void)0;/' \
    's/atomic_store_relaxed(&state->c.flags, c.flags);/(void)0;/' \
    's/(\*done)(q, ccb, &c);/membar_release(); atomic_store_relaxed(\&state->c.flags, c.flags); (*done)(q, ccb, \&c);/' \
    's/(\*done)(q, ccb, &c);/membar_release(); (*done)(q, ccb, \&c);/; s/^	membar_release();$/	(void)0;/' \
    's/^		membar_acquire();$/		(void)0;/; s/^	nvme_q_submit(sc, q, ccb, nvme_poll_fill);$/	membar_acquire(); nvme_q_submit(sc, q, ccb, nvme_poll_fill);/' \
    's|^	/\* state may be gone now \*/$|	state->cookie = NULL;|' \
    's/(\*state.done)(q, ccb, &cqe);/mutex_enter(\&q->q_cq_mtx); (*state.done)(q, ccb, \&cqe); mutex_exit(\&q->q_cq_mtx);/' \
    's/ccb->ccb_done = nvme_empty_done;/(void)0;/' \
    's/(\*state.done)(q, ccb, &cqe);/(void)0;/' \
    's/->claimed = true;/->claimed = false;/' \
    's/if (state.claimed) {/if (0) {/' \
    's/mutex_enter(&q->q_cq_mtx);/(void)0;/'; do
	sed "$m" "$work/orig.h" > "$work/nvme-poll.h"
	if cmp -s "$work/orig.h" "$work/nvme-poll.h"; then
		echo "mutation did not apply: $m" >&2
		exit 1
	fi
	build "$work/mutant"
	# A mutant may crash; its own shell reports that, into /dev/null.
	if ( ${TEST_RUNNER:-} "$work/mutant"; exit $? ) > /dev/null 2>&1; then
		echo "mutation not caught: $m" >&2
		exit 1
	fi
	caught=$((caught + 1))
done
echo "nvme poll mutations caught: $caught"
