# $NetBSD: features.mk,v 1.2 2015/06/07 15:05:34 matt Exp $

.ifnmake obj
TESTFILE=${NETBSDSRCDIR}/common/lib/libc/arch/arm/features.c
# Syntax-only checks select their own compiler action.
.for feature in EABI LDREX LDRD THUMB2
FEAT_${feature}!=if ${COMPILE.c:N-c} ${COPTS} -fsyntax-only -D${feature}_TEST ${TESTFILE} >/dev/null 2>/dev/null; then echo yes; else echo no; fi
.endfor
.endif
