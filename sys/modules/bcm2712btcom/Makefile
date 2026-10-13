.include "../Makefile.inc"
.PATH: ${S}/dev/ic
KMOD=bcm2712btcom
EMBER_KERNEL_CONFIG?=EMBER64
EMBER_KERNEL_OBJDIR?=${S}/arch/evbarm/compile/${EMBER_KERNEL_CONFIG}
CPPFLAGS+=-I${EMBER_KERNEL_OBJDIR}
SRCS=bcm2712btcom.c
.include <bsd.kmodule.mk>
