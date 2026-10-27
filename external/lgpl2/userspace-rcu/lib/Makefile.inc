# $NetBSD: Makefile.inc,v 1.1 2025/01/17 16:07:26 christos Exp $
.include "${.PARSEDIR}/../Makefile.inc"

# EmberBSD: Clang -Walloc-size on per-CPU call_rcu_data array (urcu-call-rcu-impl.h).
CWARNFLAGS.clang+=	-Wno-error=alloc-size
