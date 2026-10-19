#!/bin/sh
# Origin: EmberBSD i915 user engine replacement regression, 2026-10-19.
# SPDX-License-Identifier: BSD-2-Clause
# Compile the actual Linux rcupdate.h shim with host models of its kernel headers.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/linux-rcu.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/include/sys" "$work/include/linux"
cp "$src/sys/external/bsd/common/include/linux/rcupdate.h" "$work/include/linux/"
cat > "$work/include/sys/atomic.h" <<'H'
#define atomic_store_release(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define atomic_load_consume(p) __atomic_load_n((p), __ATOMIC_CONSUME)
#define atomic_load_relaxed(p) __atomic_load_n((p), __ATOMIC_RELAXED)
H
cat > "$work/include/sys/cdefs.h" <<'H'
#if __has_include_next(<sys/cdefs.h>)
#include_next <sys/cdefs.h>
#endif
H
cat > "$work/include/sys/systm.h" <<'H'
static inline void kpreempt_disable(void) { }
static inline void kpreempt_enable(void) { }
H
cat > "$work/include/linux/bug.h" <<'H'
extern int warnings;
static inline int warn_on(int c) { warnings += c; return c; }
#define WARN_ON(c) warn_on(!!(c))
H
# The only consumer frees the returned pointer as the old engine set.
grep -q 'set.engines = rcu_replace_pointer(ctx->engines, set.engines, 1);' \
    "$src/sys/external/bsd/drm2/dist/drm/i915/gem/i915_gem_context.c"
# shellcheck disable=SC2086
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror ${CFLAGS:-} -I"$work/include" \
    "$src/ember/tools/linux-rcu-replace-contract.c" -o "$work/contract"
"$work/contract"
