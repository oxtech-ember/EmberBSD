#	$NetBSD: Makefile,v 1.1 2025/07/17 14:25:20 christos Exp $

SUBDIR=		lib .WAIT bin

.if defined(EMBER_OPENSSL_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
# Generate once at this parent before any recursive header or library target.
# The build's selected tools and target object directory remain authoritative.
EMBER_OPENSSL_GENERATED?= ${.OBJDIR}/generated
.MAKEFLAGS: EMBER_OPENSSL_GENERATED=${EMBER_OPENSSL_GENERATED:Q}
.if !defined(HOST_PYTHON) || !defined(HOST_PERL) || !defined(HOST_GMAKE) || !defined(HOST_SH)
.error OpenSSL generation requires selected HOST_PYTHON, HOST_PERL, HOST_GMAKE and HOST_SH
.endif
.PHONY: openssl-generated
openssl-generated:
	${HOST_PYTHON} -B ${.CURDIR}/tools/prepare-generated.py \
		    --source ${EMBER_OPENSSL_SOURCE:Q} --output ${EMBER_OPENSSL_GENERATED:Q} --object-root ${.OBJDIR:Q} \
		    --arch ${MACHINE_ARCH:Q} --cpu ${MACHINE_CPU:Q} --triplet ${MACHINE_GNU_PLATFORM:Q} --family ${ACTIVE_CC:Q} --cc ${CC:[1]:Q} --cc-command=${CC:Q} \
		    --cc-flags=${CFLAGS:Q} --cc-options=${COPTS:Q} --sysroot ${DESTDIR:Q} \
		    --perl ${HOST_PERL:Q} --gmake ${HOST_GMAKE:Q} --shell ${HOST_SH:Q} \
		    --ar ${AR:[1]:Q} --ranlib ${RANLIB:[1]:Q} \
		    --tmpdir ${TMPDIR:U${.OBJDIR}:Q} --path ${PATH:Q} --jobs ${MAKE_JOBS:U1:Q}
.if ${EMBER_OPENSSL_GENERATION_READY:Uno} != "yes"
.for target in all depend dependall includes install
${target}-lib: openssl-generated
.endfor
.endif
.endif

.include <bsd.subdir.mk>
.include <bsd.sys.mk>
