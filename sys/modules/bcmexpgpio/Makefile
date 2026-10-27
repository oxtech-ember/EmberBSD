.include "../Makefile.inc"
.PATH: ${S}/arch/arm/broadcom
KMOD=bcmexpgpio
EMBER_KERNEL_CONFIG?=EMBER64
EMBER_KERNEL_OBJDIR?=${S}/arch/evbarm/compile/${EMBER_KERNEL_CONFIG}
CPPFLAGS+=-I${EMBER_KERNEL_OBJDIR}
SRCS=bcm2835_expgpio.c
.include <bsd.kmodule.mk>
