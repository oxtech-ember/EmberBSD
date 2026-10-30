#	$NetBSD: bsd.hostlib.mk,v 1.21 2024/04/12 16:28:43 jakllsch Exp $

.include <bsd.hostinit.mk>
.include <bsd.sys.mk>

##### Basic targets

##### Default values
CFLAGS+=	${COPTS}
MKDEP_SUFFIXES?=	.o .lo .d

# Override these:
_MKDEP_TARGET_FLAGS=	# Native dependencies must not inherit target flags.
# nbhost-mkdep accepts only its documented preprocessor option subset.
_MKDEP_CFLAGS=		${HOST_CFLAGS:M-std=*} ${HOST_CFLAGS:C/-([IDUW])[ 	]*/-\1/Wg:M-[IDUW]*}
_MKDEP_CXXFLAGS=	${CXXFLAGS:M-std=*} ${CXXFLAGS:C/-([IDUW])[ 	]*/-\1/Wg:M-[IDUW]*}
_MKDEP_OBJCFLAGS=	${OBJCFLAGS:C/-([IDUW])[ 	]*/-\1/Wg:M-[IDUW]*}
_MKDEP_AFLAGS=		${AFLAGS:C/-([IDUW])[ 	]*/-\1/Wg:M-[IDUW]*}
_MKDEP_FILE_CPUFLAGS=	# No target per-file ABI flags for native tools.
MKDEP:=		${HOST_MKDEP}
MKDEPCXX:=	${HOST_MKDEPCXX}

.if ${TOOLCHAIN_MISSING} == "no" || defined(EXTERNAL_TOOLCHAIN)
OBJHOSTMACHINE=	# set
.endif

##### Build rules
.if defined(HOSTLIB)
_YHLSRCS=	${SRCS:M*.[ly]:C/\..$/.c/} ${YHEADER:D${SRCS:M*.y:.y=.h}}
DPSRCS+=	${_YHLSRCS}
CLEANFILES+=	${_YHLSRCS}
.endif	# defined(HOSTLIB)

.if !empty(SRCS:N*.h:N*.sh)
OBJS+=		${SRCS:N*.h:N*.sh:R:S/$/.lo/g}
.endif

.if defined(OBJS) && !empty(OBJS)
.NOPATH: lib${HOSTLIB}.a ${OBJS} ${_YHLSRCS}

${OBJS}: ${DPSRCS}

lib${HOSTLIB}.a: ${OBJS} ${DPADD}
	${_MKTARGET_BUILD}
	rm -f ${.TARGET}
	${HOST_AR} cq ${.TARGET} ${OBJS}
	${HOST_RANLIB} ${.TARGET}

.endif	# defined(OBJS) && !empty(OBJS)

realall: lib${HOSTLIB}.a

CLEANFILES+= a.out [Ee]rrs mklog core *.core lib${HOSTLIB}.a ${OBJS}

beforedepend:
CFLAGS:=	${HOST_CFLAGS}
CPPFLAGS:=	${HOST_CPPFLAGS:N-Wp,-iremap,*}

##### Pull in related .mk logic
.include <bsd.obj.mk>
.include <bsd.dep.mk>
.include <bsd.clean.mk>

${TARGETS}:	# ensure existence
