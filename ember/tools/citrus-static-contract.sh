#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run complete native Citrus translation units with host services and real data.
set -eu
ulimit -c 0
[ "$#" -eq 0 ] || exit 2
analyze=${CITRUS_ANALYZE:-0}
compile_only=${CITRUS_COMPILE_ONLY:-0}
mode=${CITRUS_TEST_MODE:-all}
case "$analyze" in 0|1) ;; *) exit 2;; esac
case "$compile_only" in 0|1) ;; *) exit 2;; esac
case "$mode" in all|lookup|lookup-db|lookup-plain|convert|cleanup|state-error|ctype-error|stdenc-output) ;; *) exit 2;; esac
if [ "$compile_only" = 1 ] && [ -z "${CITRUS_TEST_OUTPUT:-}" ]; then
    echo 'compile-only requires a new CITRUS_TEST_OUTPUT directory' >&2
    exit 2
fi
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
if [ -n "${CITRUS_TEST_OUTPUT:-}" ]; then
    case "$CITRUS_TEST_OUTPUT" in /*) ;; *) exit 2;; esac
    mkdir "$CITRUS_TEST_OUTPUT"
    work=$CITRUS_TEST_OUTPUT
else
    work=$(mktemp -d "${TMPDIR:-/tmp}/citrus-static.XXXXXXXX")
    trap 'rm -rf "$work"' EXIT HUP INT TERM
fi
mkdir -p "$work/include/sys" "$work/data/lookup" "$work/data/iconv" \
    "$work/data/esdb/UTF" "$work/data/esdb/ISO646" "$work/data/csmapper/ISO646"
cp "$src"/lib/libc/citrus/modules/*.h "$work/include/"
cp "$src"/lib/libc/citrus/*.h "$work/include/"
cp "$src/common/lib/libc/stdlib/_strtol.h" "$src/common/lib/libc/stdlib/_strtoul.h" "$work/include/"
cp "$src/sys/sys/queue.h" "$work/include/sys/queue.h"
cp "$src/ember/tools/citrus-static-contract-compat.h" "$work/include/compat.h"
: > "$work/include/namespace.h"
cat > "$work/include/reentrant.h" <<'HDR'
/* Single-thread host model. This test makes no concurrency claim. */
#define rwlock_wrlock(x) ((void)0)
#define rwlock_rdlock(x) ((void)0)
#define rwlock_unlock(x) ((void)0)
HDR
printf '#include "compat.h"\n' > "$work/include/sys/endian.h"
printf '#include "compat.h"\n' > "$work/include/endian.h"
cat > "$work/include/paths.h" <<HDR
#define _PATH_ICONV "$work/data/iconv"
#define _PATH_ESDB "$work/data/esdb"
#define _PATH_CSMAPPER "$work/data/csmapper"
HDR
cat > "$work/include/iconv.h" <<'HDR'
#include <stddef.h>
#include <stdint.h>
typedef void *iconv_t;
iconv_t iconv_open(const char *, const char *);
int iconv_close(iconv_t);
size_t iconv(iconv_t, char **, size_t *, char **, size_t *);
size_t __iconv(iconv_t, char **, size_t *, char **, size_t *, uint32_t, size_t *);
HDR
parts='citrus_bcs citrus_bcs_strtol citrus_bcs_strtoul citrus_csmapper citrus_db citrus_db_hash citrus_esdb citrus_hash citrus_iconv citrus_lookup citrus_mapper citrus_memstream citrus_mmap citrus_module citrus_module_static citrus_none citrus_stdenc'
for part in $parts; do cp "$src/lib/libc/citrus/$part.c" "$work/$part.c"; done
for part in citrus_iconv_std citrus_utf8 citrus_mapper_646 citrus_mapper_none; do
    cp "$src/lib/libc/citrus/modules/$part.c" "$work/$part.c"
done
cp "$src/common/lib/libc/string/strlcpy.c" "$work/strlcpy.c"
cp "$src/lib/libc/iconv/iconv.c" "$work/public_iconv.c"
cp "$src/ember/tools/citrus-static-contract-data.h" "$work/include/"
cp "$src/ember/tools/citrus-static-contract.c" "$work/test.c"
if [ "$(uname -s)" = Darwin ]; then
    SDKROOT=${SDKROOT:-$(xcrun --show-sdk-path)}
    export SDKROOT
    set -- -isysroot "$SDKROOT"
else
    set --
fi
: > "$work/commands.jsonl"
for file in "$work"/*.c; do
    alloc='-Dmalloc=citrus_test_malloc -Dcalloc=citrus_test_calloc -Drealloc=citrus_test_realloc -Dfree=citrus_test_free -Dstrdup=citrus_test_strdup'
    [ "${file##*/}" != test.c ] || alloc=
    # CFLAGS is a caller-supplied compiler argument list. ARGSUSED handling
    # matches native libc flags. No analyzer category is disabled.
    ${CC:-cc} -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror ${CFLAGS:-} "$@" \
        -I"$work/include" -include "$work/include/compat.h" \
        $alloc -c "$file" -o "${file%.c}.o"
    ruby -rjson -e 'p,f,*a=ARGV; File.open(p,"a") { |o| o.puts JSON.generate(directory: File.dirname(f),file:f,arguments:a) }' \
        "$work/commands.jsonl" "$file" "${CC:-cc}" -std=gnu11 -Wall -Wextra \
        -Wno-unused-parameter -Werror ${CFLAGS:-} "$@" -I"$work/include" \
        -include "$work/include/compat.h" $alloc -c "$file" -o "${file%.c}.o"
    if [ "$analyze" = 1 ]; then
        case "${file##*/}" in
            citrus_module*.c|citrus_stdenc.c|citrus_lookup.c|citrus_iconv_std.c|citrus_utf8.c|citrus_mapper_*.c|test.c)
                if ! "${CSA:-clang}" --analyze -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Werror \
                    "$@" -I"$work/include" -include "$work/include/compat.h" $alloc \
                    "$file" -o "${file%.c}.plist" 2> "${file%.c}.csa.log"; then
                    cat "${file%.c}.csa.log"
                    exit 1
                fi
                cat "${file%.c}.csa.log"
                test ! -s "${file%.c}.csa.log"
                ;;
        esac
    fi
done
ruby -rjson -e 'puts JSON.pretty_generate(File.readlines(ARGV[0]).map { |l| JSON.parse(l) })' \
    "$work/commands.jsonl" > "$work/compile_commands.json"
if [ "$analyze" = 1 ]; then
    ruby -rjson -e 'a=JSON.parse(File.read(ARGV[0])); puts JSON.pretty_generate(a.select { |e| File.basename(e["file"]).match?(/\A(?:citrus_module.*|citrus_stdenc|citrus_lookup|citrus_iconv_std|citrus_utf8|citrus_mapper_.*|test)\.c\z/) })' \
        "$work/compile_commands.json" > "$work/analysis_commands.json"
    if ! cppcheck --quiet --check-level=exhaustive --enable=warning,performance,portability \
        --error-exitcode=1 --project="$work/analysis_commands.json" --platform=unix64 \
        2> "$work/cppcheck.log"; then
        cat "$work/cppcheck.log"
        exit 1
    fi
    cat "$work/cppcheck.log"
fi
${CC:-cc} ${CFLAGS:-} "$@" "$work"/*.o -o "$work/contract"
[ "$compile_only" = 0 ] || exit 0
run_case()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        (cd "$work" && "$TEST_RUNNER" "$work/contract" "$@")
    else
        (cd "$work" && ./contract "$@")
    fi
}
run_case init
cp "$src/share/i18n/iconv/iconv.dir" "$work/data/iconv/"
cp "$src/share/i18n/csmapper/ISO646/ISO646-US%UCS.646" "$work/data/csmapper/ISO646/"
printf 'UTF-8 UTF/UTF-8.esdb\nISO646-US ISO646/ISO646-US.esdb\n' > "$work/data/esdb/esdb.dir"
printf 'ASCII ISO646-US\n646 ISO646-US\nus-ascii ISO646-US\nUTF8 UTF-8\n' > "$work/data/esdb/esdb.alias"
printf 'ISO646-US/UCS mapper_646 ISO646/ISO646-US%%UCS.646\nUCS/ISO646-US mapper_646 !ISO646/ISO646-US%%UCS.646\n' > "$work/data/csmapper/mapper.dir"
printf 'ascii ISO646-US\n646 ISO646-US\n' > "$work/data/lookup/plain"
if [ "$mode" != all ]; then run_case "$mode"; exit 0; fi
for part in lookup-db lookup-plain lookup convert cleanup state-error ctype-error stdenc-output; do
    run_case "$part"
done
n=1
while [ "$n" -le 96 ]; do
    run_case oom-open "$n"
    n=$((n + 1))
done
for file in iconv/iconv.dir esdb/esdb.dir esdb/esdb.alias esdb/UTF/UTF-8.esdb esdb/ISO646/ISO646-US.esdb csmapper/mapper.dir csmapper/ISO646/ISO646-US%UCS.646; do
    mv "$work/data/$file" "$work/data/$file.saved"
    run_case missing
    mv "$work/data/$file.saved" "$work/data/$file"
done
