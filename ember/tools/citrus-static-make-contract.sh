#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Query the native archive rules. Do not compile or generate target objects.
set -eu
[ "$#" -eq 0 ] || exit 2
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/citrus-make.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat > "$work/Makefile" <<'MK'
.include <bsd.own.mk>
LIB= citruscontract
LIBCDIR= ${.CURDIR}
ARCHDIR= ${.CURDIR}/arch/aarch64
.include "${.CURDIR}/citrus/Makefile.inc"
.include <bsd.lib.mk>
.for s in ${CITRUS_STATIC_SRCS}
.if empty(STOBJS:M${s:R}.o)
.error Static Citrus object missing: ${s}
.endif
.if empty(__DPSRCS.d:M${s:R}.d)
.error Static Citrus dependency missing: ${s}
.endif
.if !empty(SOBJS:M${s:R}.pico) || !empty(SRCS:M${s})
.error Static Citrus module entered shared libc: ${s}
.endif
.if ${MKPROFILE} == "yes" && empty(POBJS:M${s:R}.po)
.error Profile Citrus object missing: ${s}
.endif
.if ${MKDEBUGLIB} == "yes" && empty(GOBJS:M${s:R}.go)
.error Debug Citrus object missing: ${s}
.endif
.endfor
.if ${MKPIC} == "yes" && empty(SOBJS:Mcitrus_module.pico)
.error Dynamic Citrus loader missing from shared libc
.endif
MK
reject()
{
    if "$@" > "$work/rejected.log" 2>&1; then
        echo 'runner accepted an invalid option' >&2
        exit 1
    else
        test "$?" -eq 2
    fi
}
for option in CITRUS_ANALYZE=2 CITRUS_COMPILE_ONLY=2 CITRUS_TEST_MODE=bad CITRUS_COMPILE_ONLY=1; do
    reject env CITRUS_ANALYZE=0 CITRUS_COMPILE_ONLY=0 CITRUS_TEST_MODE=all \
        CITRUS_TEST_OUTPUT= "$option" sh "$src/ember/tools/citrus-static-contract.sh"
done
reject sh "$src/ember/tools/citrus-static-contract.sh" unexpected
for variant in static shared; do

    pic=no
    profile=no
    [ "$variant" != shared ] || { pic=yes; profile=yes; }
    MAKEOBJDIR="$work" "${CITRUS_MAKE:-bmake}" -C "$src/lib/libc" -f "$work/Makefile" \
        -m "$src/share/mk" NETBSDSRCDIR="$src" MACHINE=evbarm MACHINE_ARCH=aarch64 \
        USETOOLS=no TOOLCHAIN_MISSING=yes MKOBJDIRS=no MKLINT=no \
        MKPIC="$pic" MKPICLIB="$pic" MKPROFILE="$profile" MKDEBUGLIB="$profile" \
        MAKELINKLIB=yes -V '${STOBJS:M*citrus*}' -V '${SOBJS:M*citrus*}' \
        -V '${POBJS:M*citrus*}' -V '${GOBJS:M*citrus*}'
done
printf '%s\n' 'Native Citrus static/shared archive selection: PASS'
