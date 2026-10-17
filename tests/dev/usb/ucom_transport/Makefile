# Origin: EmberBSD private external-ucom rump fixture, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
.include <bsd.own.mk>
RUMPTOP= ${NETBSDSRCDIR}/sys/rump
.PATH: ${NETBSDSRCDIR}/sys/dev/usb
LIB= rumpdev_ucom_mock
LIBISPRIVATE= yes
IOCONF= MOCK.ioconf
SRCS= ucom.c ucom_transport_core.c ucom_transport_rx.c
SRCS+= umoxa_frame.c umoxa_g2_txq.c
SRCS+= umock.c component.c usb_stubs.c
CPPFLAGS+= -I${.CURDIR} -I${RUMPTOP}/librump/rumpkern
CPPFLAGS+= -I${RUMPTOP}/dev/lib/libucom
.include "${RUMPTOP}/Makefile.rump"
.include <bsd.lib.mk>
.include <bsd.klinks.mk>
