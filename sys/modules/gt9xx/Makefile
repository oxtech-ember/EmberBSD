# Origin: EmberBSD - build the Goodix pointer driver against the target kernel.
.include "../Makefile.inc"
.PATH: ${S}/dev/i2c
KMOD=gt9xx
IOCONF=gt9xx.ioconf
SRCS=gt9xx.c gt9xx_frame.c gt9xx_ys_m33.c
.include <bsd.kmodule.mk>
