# Origin: EmberBSD private native G2 parent fixture, 2026-10-14.
# SPDX-License-Identifier: BSD-2-Clause
.include <bsd.own.mk>
RUMPTOP= ${NETBSDSRCDIR}/sys/rump
.PATH: ${NETBSDSRCDIR}/sys/dev/usb
LIB= rumpdev_umoxag_mock
LIBISPRIVATE= yes
IOCONF= MOCKG.ioconf
SRCS= ucom.c ucom_transport_core.c ucom_transport_rx.c
SRCS+= umoxa_frame.c umoxa_g2_txq.c umoxa_g2_protocol.c
SRCS+= umoxa_g2.c umoxa_g2_io.c umoxa_g2_tty.c
SRCS+= component.c ug2mock.c usb_stubs.c
CPPFLAGS+= -I${.CURDIR} -I${RUMPTOP}/librump/rumpkern
CPPFLAGS+= -I${RUMPTOP}/dev/lib/libucom
.include "${RUMPTOP}/Makefile.rump"
.include <bsd.lib.mk>
.include <bsd.klinks.mk>
