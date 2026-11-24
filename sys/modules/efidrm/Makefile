# efidrm: KMS over the UEFI GOP framebuffer (loadable module).
.include "../Makefile.inc"
.include "../drmkms/Makefile.inc"

KMOD=	efidrm

.PATH:	${S}/external/bsd/drm2/efidrm
.PATH:	${S}/external/bsd/drm2/drm

SRCS+=	efidrm.c
SRCS+=	drm_gem_shmem_helper.c

CPPFLAGS+=	-I${S}/sys/modules/drmkms

# pmap_enter is only a weak alias on x86: the module loader does not resolve it
CPPFLAGS+=	-Dpmap_enter=pmap_enter_default

# The imported drm2 and Linux headers do not pass -Wsign-compare.  Disable
# only this warning, as the kernel build does (sys/conf/Makefile.kern.inc).
CWARNFLAGS+=	-Wno-sign-compare

# With the objects of a selected kernel, bind the module to that kernel:
# efidrm.c then refuses to load into a kernel with another version string.
.if !empty(EMBER_KERNEL_OBJDIR:U)
DPSRCS+=	efidrm_kver.h
CLEANFILES+=	efidrm_kver.h efidrm_kver.h.tmp
CPPFLAGS+=	-I${.OBJDIR}

efidrm_kver.h: ${EMBER_KERNEL_OBJDIR}/vers.c ${.CURDIR}/kver.awk
	${_MKTARGET_CREATE}
	${TOOL_AWK} -f ${.CURDIR}/kver.awk ${EMBER_KERNEL_OBJDIR}/vers.c \
	    > ${.TARGET}.tmp
	mv -f ${.TARGET}.tmp ${.TARGET}
# Also for an object of an earlier build without the header.
efidrm.o: efidrm_kver.h
.endif

.include <bsd.kmodule.mk>
