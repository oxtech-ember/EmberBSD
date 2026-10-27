.include "../Makefile.inc"
.PATH: ${S}/arch/arm/broadcom
KMOD=bcmexpgpio
OXTORG_KERNEL_CONFIG?=OXTORG64
CPPFLAGS+=-I${S}/arch/evbarm/compile/${OXTORG_KERNEL_CONFIG}
SRCS=bcm2835_expgpio.c
.include <bsd.kmodule.mk>
