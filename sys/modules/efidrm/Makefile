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

CWARNFLAGS+=	-Wno-unused-function -Wno-cast-qual -Wno-pointer-arith

.include <bsd.kmodule.mk>
