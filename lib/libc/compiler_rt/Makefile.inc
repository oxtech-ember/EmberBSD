# $NetBSD: Makefile.inc,v 1.54 2025/04/06 23:03:05 rillig Exp $
# Origin: EmberBSD; AI-assisted source-local AArch64 binary128 FENV opt-in.

COMPILER_RT_DIR=	${NETBSDSRCDIR}/sys/external/bsd/compiler_rt
.include <bsd.llvm-source.mk>
.if !defined(EMBER_LLVM_SOURCE)
.error Set EMBER_LLVM_SOURCE to the common Ports-prepared LLVM source root
.endif
COMPILER_RT_SRCDIR=	${EMBER_COMPILER_RT_SOURCE}
.for _ember_rt_file in lib/builtins/int_lib.h lib/builtins/comparetf2.c \
    include/profile/InstrProfData.inc
.if !exists(${COMPILER_RT_SRCDIR}/${_ember_rt_file})
.error EMBER_LLVM_SOURCE is missing compiler-rt/${_ember_rt_file}
.endif
.endfor
CPPFLAGS+=	-I${COMPILER_RT_SRCDIR}/include

.if ${LIBC_MACHINE_ARCH} == "powerpc" || ${LIBC_MACHINE_ARCH} == "powerpc64"
COMPILER_RT_CPU_DIR=	${COMPILER_RT_SRCDIR}/lib/builtins/ppc
COMPILER_RT_ARCH_DIR=	${COMPILER_RT_SRCDIR}/lib/builtins/ppc
.else
COMPILER_RT_CPU_DIR=	${COMPILER_RT_SRCDIR}/lib/builtins/${LIBC_MACHINE_CPU}
COMPILER_RT_ARCH_DIR=	${COMPILER_RT_SRCDIR}/lib/builtins/${LIBC_MACHINE_ARCH}
.endif

.PATH: ${COMPILER_RT_CPU_DIR}
.PATH: ${COMPILER_RT_ARCH_DIR}
.PATH: ${COMPILER_RT_SRCDIR}/lib/builtins
.PATH: ${COMPILER_RT_SRCDIR}/lib/profile

# Complex support needs parts of libm
#GENERIC_SRCS+=
#	mulxc3.c \
#	mulsc3.c \
#	divxc3.c \
#	divdc3.c \
#	divsc3.c

# Implemented on top of our atomic interface.
#GENERIC_SRCS+= atomic.c

.if ${HAVE_LIBGCC_EH} == "no"
GENERIC_SRCS+= \
	gcc_personality_v0.c
.endif

GENERIC_SRCS+= \
	absvsi2.c \
	absvti2.c \
	addvsi3.c \
	addvti3.c \
	ashlti3.c \
	ashrti3.c \
	clzti2.c \
	cmpti2.c \
	ctzti2.c \
	divti3.c \
	ffsti2.c \
	fixsfdi.c \
	fixdfdi.c \
	fixunsdfdi.c \
	fixunsdfsi.c \
	fixunssfdi.c \
	fixunssfsi.c \
	int_util.c \
	lshrti3.c \
	modti3.c \
	muldc3.c \
	mulosi4.c \
	muloti4.c \
	multi3.c \
	mulvsi3.c \
	mulvti3.c \
	negti2.c \
	negvsi2.c \
	negvti2.c \
	paritysi2.c \
	parityti2.c \
	popcountsi2.c \
	popcountti2.c \
	subvsi3.c \
	subvti3.c \
	ucmpti2.c \
	udivmodti4.c \
	udivti3.c \
	umodti3.c

# only used for machines using IEEE 754 floating point formats
.if ${LIBC_MACHINE_ARCH} != "vax"

.if 0
# Conflicts with soft-float
GENERIC_SRCS+= \
	comparedf2.c \
	comparesf2.c \
	adddf3.c \
	addsf3.c \
	addtf3.c \
	divdf3.c \
	divsf3.c \
	divtf3.c \
	extendsfdf2.c \
	extendsftf2.c \
	extenddftf2.c \
	fixdfsi.c \
	fixdfti.c \
	fixsfsi.c \
	fixsfti.c \
	floatsidf.c \
	floatsisf.c \
	floatunsidf.c \
	floatunsisf.c \
	muldf3.c \
	mulsf3.c \
	multf3.c \
	subdf3.c \
	subsf3.c \
	subtf3.c \
	truncdfsf2.c \
	trunctfdf2.c \
	trunctfsf2.c
.endif

GENERIC_SRCS+=	\
	floatdidf.c \
	floatdisf.c \
	floatundidf.c \
	floatundisf.c \
	negdf2.c \
	negsf2.c \
	powidf2.c \
	powisf2.c \
	powitf2.c

.endif # IEEE 754 only machines

# LLVM compiler-rt exposes xf_float/xf_bits only on its supported 80-bit
# long-double targets.  Keep the NetBSD m68k/ia64 ABI sources available even
# where the selected Clang cannot validate those target ABIs.
.if ${LIBC_MACHINE_ARCH} == "x86_64" || ${LIBC_MACHINE_ARCH} == "i386" || \
    ${LIBC_MACHINE_ARCH} == "m68k" || ${LIBC_MACHINE_ARCH} == "ia64"
GENERIC_SRCS+= \
	fixunsxfdi.c \
	fixunsxfsi.c \
	floatdixf.c \
	floatundixf.c \
	powixf2.c \
	fixunsxfti.c \
	fixxfti.c \
	floattixf.c \
	floatuntixf.c
. if ${LIBC_MACHINE_ARCH} != "m68k"
GENERIC_SRCS+= \
	fixxfdi.c
.endif
.endif


.if ${LIBC_MACHINE_CPU} == "aarch64"
GENERIC_SRCS+= \
	comparetf2.c
LINTFLAGS.comparetf2.c += -X 132
.if ${MKSOFTFLOAT:Uno} == "no"
CPPFLAGS.comparetf2.c+= -DCOMPILER_RT_NETBSD_AARCH64_FENV
.endif
.endif

GENERIC_SRCS+= \
	fixunsdfti.c \
	fixunssfti.c \
	floattidf.c \
	floattisf.c \
	floatuntidf.c \
	floatuntisf.c

# These have h/w instructions which are always used.
.if ${LIBC_MACHINE_ARCH} != "alpha" && ${LIBC_MACHINE_CPU} != "aarch64" \
    && ${LIBC_MACHINE_CPU} != "powerpc" && ${LIBC_MACHINE_CPU} != "or1k"
GENERIC_SRCS+= \
	clzsi2.c
.endif

# These have h/w instructions which are always used.
.if ${LIBC_MACHINE_ARCH} != "alpha" && ${LIBC_MACHINE_ARCH} != "vax" \
    && ${LIBC_MACHINE_CPU} != "aarch64" && ${LIBC_MACHINE_CPU} != "powerpc" \
    && ${LIBC_MACHINE_CPU} != "or1k"
GENERIC_SRCS+= \
	ctzsi2.c
.endif

# These have h/w instructions which are always used.
.if ${LIBC_MACHINE_ARCH} != "alpha" && ${LIBC_MACHINE_CPU} != "powerpc" \
    && ${LIBC_MACHINE_CPU} != "aarch64" && ${LIBC_MACHINE_ARCH} != "vax"
GENERIC_SRCS+= \
	divmodsi4.c \
	divsi3.c \
	modsi3.c \
	udivmodsi4.c \
	umodsi3.c

. if ${LIBC_MACHINE_CPU} != "sh3"
# On sh3 __udivsi3 is gcc "millicode" with special calling convention
# (less registers clobbered than usual).  Each DSO that needs it gets
# its own hidden copy from libgcc.a.
GENERIC_SRCS+= \
	udivsi3.c
. endif
.endif


GENERIC_SRCS+= \
	absvdi2.c \
	addvdi3.c \
	mulodi4.c \
	mulvdi3.c \
	negvdi2.c \
	paritydi2.c \
	popcountdi2.c \
	subvdi3.c

# These have h/w instructions which are always used.
.if ${LIBC_MACHINE_ARCH} != "alpha" && ${LIBC_MACHINE_CPU} != "powerpc64" \
    && ${LIBC_MACHINE_CPU} != "aarch64" && ${LIBC_MACHINE_CPU} != "or1k"
GENERIC_SRCS+= \
	clzdi2.c \
	ctzdi2.c \
	ffsdi2.c
.endif

# Don't need these on 64-bit machines.
.if empty(LIBC_MACHINE_ARCH:M*64*) && ${LIBC_MACHINE_ARCH} != "alpha"
GENERIC_SRCS+= \
	cmpdi2.c \
	ashldi3.c \
	ashrdi3.c \
	divdi3.c \
	divmoddi4.c \
	lshrdi3.c \
	moddi3.c \
	muldi3.c \
	negdi2.c \
	ucmpdi2.c \
	udivdi3.c \
	udivmoddi4.c \
	umoddi3.c
.endif

GENERIC_SRCS+= \
	GCDAProfiling.c \
	InstrProfiling.c \
	InstrProfilingBuffer.c \
	InstrProfilingFile.c \
	InstrProfilingPlatformLinux.c \
	InstrProfilingUtil.c \
	InstrProfilingValue.c \
	InstrProfilingWriter.c

# Modern compiler-rt splits the profiling implementation across these units.
# Preserve the old public libc GCDA entry points through the common Ports
# compatibility mode; ordinary profiling runtimes retain the upstream ABI.
GENERIC_SRCS+= \
	InstrProfilingInternal.c \
	InstrProfilingMerge.c \
	InstrProfilingMergeFile.c \
	InstrProfilingNameVar.c \
	InstrProfilingVersionVar.c
CPPFLAGS.GCDAProfiling.c+= -DCOMPILER_RT_LIBC_LEGACY_GCOV
.for _ember_profile_src in GCDAProfiling.c InstrProfilingUtil.c
CPPFLAGS.${_ember_profile_src}+= -DCOMPILER_RT_HAS_FCNTL_LCK=1 -DCOMPILER_RT_HAS_UNAME=1
.endfor

.if ${MKPROFILE} != "no"
POBJS+= InstrProfilingRuntime.po
.endif

.if ${LIBC_MACHINE_ARCH} == "powerpc" || ${LIBC_MACHINE_ARCH} == "powerpc64"
GENERIC_SRCS+= \
	fixtfdi.c \
	fixunstfdi.c \
	floatditf.c \
	floatunditf.c \
	gcc_qadd.c \
	gcc_qdiv.c \
	gcc_qmul.c \
	gcc_qsub.c
.endif

.if ${LIBC_MACHINE_CPU} == "aarch64"
# addtf3.c reads the rounding mode and raises INEXACT through the
# __fe_getround and __fe_raise_inexact hooks in aarch64/fp_mode.c.
GENERIC_SRCS+= \
	clear_cache.c \
	fp_mode.c \
	addtf3.c \
	fp_mode.c \
	divtf3.c \
	multf3.c \
	subtf3.c \
	trunctfsf2.c \
	trunctfdf2.c \
	fixdfti.c \
	fixtfsi.c \
	fixtfdi.c \
	extendsftf2.c \
	extenddftf2.c \
	floatunsitf.c \
	floatunditf.c \
	floatsitf.c \
	floatditf.c
CPPFLAGS.fp_mode.c+= -I${COMPILER_RT_SRCDIR}/lib/builtins \
	-include ${.CURDIR}/compiler_rt/fp_mode_visibility.h
LINTFLAGS.addtf3.c += -X 132
LINTFLAGS.divtf3.c += -X 132
LINTFLAGS.extenddftf2.c += -X 117,132
LINTFLAGS.extendsftf2.c += -X 117,132
LINTFLAGS.fixdfti.c += -X 132,309
LINTFLAGS.fixtfdi.c += -X 132
LINTFLAGS.fixtfsi.c += -X 132
LINTFLAGS.floatditf.c += -X 132
LINTFLAGS.floatsitf.c += -X 132
LINTFLAGS.floatunsitf.c += -X 132
LINTFLAGS.floatunditf.c += -X 132
LINTFLAGS.multf3.c += -X 132
LINTFLAGS.subtf3.c += -X 132
LINTFLAGS.trunctfdf2.c += -X 117,132
LINTFLAGS.trunctfsf2.c += -X 117,132
.endif

.if ${LIBC_MACHINE_ARCH} == "powerpc" || ${LIBC_MACHINE_ARCH} == "powerpc64"
GENERIC_SRCS+= \
	clear_cache.c
.endif

.if ${LIBC_MACHINE_ARCH} == "sparc" || ${LIBC_MACHINE_ARCH} == "sparc64"
GENERIC_SRCS+= \
	clear_cache.c
.endif

.if ${LIBC_MACHINE_CPU} == "arm"
.if !empty(LIBC_MACHINE_ARCH:Mearm*)
GENERIC_SRCS+= \
	aeabi_idivmod.S \
	aeabi_ldivmod.S \
	aeabi_uidivmod.S \
	aeabi_uldivmod.S
.endif
GENERIC_SRCS+= \
	clear_cache.c
# Not yet, overlaps with softfloat
#	aeabi_dcmp.S \
#	aeabi_fcmp.S
# Not yet, requires ARMv6
#GENERIC_SRCS+= \
#	bswapdi2.S \
#	bswapsi2.S
.endif

.for src in ${GENERIC_SRCS}
.  if exists(${COMPILER_RT_CPU_DIR}/${src:R}.S) || \
      exists(${COMPILER_RT_ARCH_DIR}/${src:R}.S)
SRCS+=	${src:R}.S
.  else
SRCS+=	${src}
.    if ${src:E} != "cc"
COPTS.${src}+=	-Wno-missing-prototypes \
		-Wno-old-style-definition \
		-Wno-strict-prototypes \
		-Wno-uninitialized \
		-Wno-cast-qual
.    endif
.  endif
.if ${MKSOFTFLOAT:Uno} != "no"
COPTS.${src}+=	-D__SOFT_FP__
.endif
.endfor

LINTFLAGS.GCDAProfiling.c += -X 123,132,351 # Ok, this is just sloppy
LINTFLAGS.GCDAProfiling.c += -X 286,287 # missing prototypes
LINTFLAGS.GCDAProfiling.c += -X 298 # vax
LINTFLAGS.InstrProfiling.c += -X 298 # vax
LINTFLAGS.InstrProfiling.c += -X 351
LINTFLAGS.InstrProfilingFile.c += -X 351
LINTFLAGS.InstrProfilingPlatformLinux.c += -X 190,351
LINTFLAGS.InstrProfilingValue.c += -X 132,231,275,351
LINTFLAGS.InstrProfilingValue.c += -X 298 # vax
LINTFLAGS.InstrProfilingWriter.c += -X 132
LINTFLAGS.absvdi2.c += -X 117,351
LINTFLAGS.absvsi2.c += -X 117,351
LINTFLAGS.absvsi3.c += -X 351
LINTFLAGS.absvti2.c += -X 117,351
LINTFLAGS.addvdi3.c += -X 351
LINTFLAGS.addvsi3.c += -X 351
LINTFLAGS.addvti3.c += -X 351
LINTFLAGS.ashlti3.c += -X 351
LINTFLAGS.ashrdi3.c += -X 117 # vax
LINTFLAGS.ashrti3.c += -X 117,351
LINTFLAGS.clzdi2.c += -X 351
LINTFLAGS.clzsi2.c += -X 351
LINTFLAGS.cmpti2.c += -X 351
LINTFLAGS.crtzi.c += -X 351
LINTFLAGS.crzdi2.c += -X 351
LINTFLAGS.ctzdi2.c += -X 351
LINTFLAGS.ctzsi2.c += -X 351
LINTFLAGS.ctzti2.c += -X 351
LINTFLAGS.divdi3.c += -X 117 # vax
LINTFLAGS.divmodsi4.c += -X 351
LINTFLAGS.divsi3.c += -X 117
LINTFLAGS.divtc3.c += -X 351	# ppc
LINTFLAGS.divti3.c += -X 117,351
LINTFLAGS.dlfcn_elf.c += -X 351
LINTFLAGS.ffsdi2.c += -X 132,351
LINTFLAGS.ffsti2.c += -X 132,351
LINTFLAGS.fixdfdi.c += -X 132,309,351
LINTFLAGS.fixdfsi.c += -X 132
LINTFLAGS.fixsfdi.c += -X 132,351
LINTFLAGS.fixunsdfdi.c += -X 132,309,351
LINTFLAGS.fixunsdfsi.c += -X 132,309,351
LINTFLAGS.fixunsdfti.c += -X 132,309,351
LINTFLAGS.fixunssfdi.c += -X 132,309,351
LINTFLAGS.fixunssfsi.c += -X 132,351
LINTFLAGS.fixunssfti.c += -X 132,351
LINTFLAGS.fixunsxfdi.c += -X 351
LINTFLAGS.fixunsxfsi.c += -X 132,309,351
LINTFLAGS.fixunsxfti.c += -X 132,351
LINTFLAGS.fixxfdi.c += -X 351
LINTFLAGS.fixxfti.c += -X 117,132,351
LINTFLAGS.floatdidf.c += -X 117	# alpha
LINTFLAGS.floatdidf.c += -X 351
LINTFLAGS.floatdisf.c += -X 117	# alpha
LINTFLAGS.floatdisf.c += -X 351
LINTFLAGS.floatdixf.c += -X 117	# alpha
LINTFLAGS.floatdixf.c += -X 351
LINTFLAGS.floattidf.c += -X 117,122,141,267,351
LINTFLAGS.floattisf.c += -X 117,351
LINTFLAGS.floattisf.c += -X 117,351
LINTFLAGS.floattixf.c += -X 117,122,351
LINTFLAGS.floattixf.c += -X 141	# alpha
LINTFLAGS.floatuntidf.c += -X 122,141,267,351
LINTFLAGS.floatuntisf.c += -X 351
LINTFLAGS.floatuntixf.c += -X 122,351
LINTFLAGS.floatuntixf.c += -X 141 # alpha
LINTFLAGS.gcc_personality_v0.c += -X 132,193,231
LINTFLAGS.int_util.c += -X 231
LINTFLAGS.lshrti3.c += -X 351
LINTFLAGS.moddi3.c += -X 117 # vax
LINTFLAGS.modsi3.c += -X 117,351
LINTFLAGS.modti3.c += -X 117,351
LINTFLAGS.muldc3.c += -X 117,351
LINTFLAGS.mulodi4.c += -X 117,351
LINTFLAGS.mulosi4.c += -X 117,351
LINTFLAGS.muloti4.c += -X 117,351
LINTFLAGS.multi3.c += -X 351
LINTFLAGS.mulvdi3.c += -X 117,351
LINTFLAGS.mulvsi3.c += -X 117,351
LINTFLAGS.mulvti3.c += -X 117,351
LINTFLAGS.negdf2.c += -X 309,351
LINTFLAGS.negsf2.c += -X 132,351
LINTFLAGS.negti2.c += -X 351
LINTFLAGS.negvdi2.c += -X 141,351
LINTFLAGS.negvsi2.c += -X 141,351
LINTFLAGS.negvti2.c += -X 141,351
LINTFLAGS.parityti2.c += -X 351
LINTFLAGS.popcountdi2.c += -X 351
LINTFLAGS.popcountsi2.c += -X 351
LINTFLAGS.popcountti2.c += -X 122,132,351
LINTFLAGS.powidf2.c += -X 351
LINTFLAGS.powisf2.c += -X 351
LINTFLAGS.powixf2.c += -X 351
LINTFLAGS.subvdi3.c += -X 351
LINTFLAGS.subvsi3.c += -X 351
LINTFLAGS.subvti3.c += -X 351
LINTFLAGS.ucmpti2.c += -X 351
LINTFLAGS.ucmpti2.c += -X 351
LINTFLAGS.udivmoddi4.c += -X 117 # vax
LINTFLAGS.udivmodti4.c += -X 117,132
LINTFLAGS.udivsi3.c += -X 117
LINTFLAGS.udivti3.c += -X 351
LINTFLAGS.umodsi3.c += -X 351
LINTFLAGS.umodti3.c += -X 351
LINTFLAGS.gcc_personality_v0.c += ${${ACTIVE_CC} == "clang":? -X 298:}

.include "${COMPILER_RT_DIR}/abi.mk"
