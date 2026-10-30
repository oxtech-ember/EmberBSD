#	$NetBSD: Makefile,v 1.13 2016/08/20 01:03:34 mrg Exp $

.include "../Makefile.serverlib"
.include "../Makefile.servermod"

LIB=		mi

.PATH:		${X11SRCDIR.xorg-server}/mi
SRCS=		miarc.c mibitblt.c micopy.c \
		micmap.c midash.c midispcur.c mieq.c \
		miexpose.c mifillarc.c mifillrct.c \
		migc.c miglblt.c mioverlay.c mipointer.c mipoly.c \
		mipolypnt.c mipolyrect.c \
		mipolyseg.c mipolytext.c mipushpxl.c \
		miscrinit.c misprite.c \
		mivaltree.c miwideline.c miwindow.c mizerarc.c \
		mizerclip.c mizerline.c

# XXX the order is important since there are headers with the same
#     name but differ depending on location; hw/xnest (for xnest-config.h)
#     stays after the xorg headers so that its Events.h never replaces
#     events.h on a case-insensitive file system
CPPFLAGS+=	-I${X11SRCDIR.xorg-server}/../include \
		-I${X11SRCDIR.xorg-server}/render \
		-I${DESTDIR}${X11INCDIR}/X11 \
		-I${X11SRCDIR.xorg-server}/Xext \
		-I${X11SRCDIR.xorg-server}/miext/damage \
		-I${X11SRCDIR.xorg-server}/Xi \
		-I${X11SRCDIR.xorg-server}/composite \
		-I${X11SRCDIR.xorg-server}/damageext \
		-I${X11SRCDIR.xorg-server}/xfixes \
		-I${DESTDIR}${X11INCDIR}/pixman-1 \
		-I${DESTDIR}${X11INCDIR}/xorg \
		-I${X11SRCDIR.xorg-server}/hw/xnest \
		${X11FLAGS.DIX} \
		-DHAVE_XNEST_CONFIG_H

CPPFLAGS.miscrinit.c=	${X11FLAGS.EXTENSION}

COPTS.micmap.c+=	-Wno-error
COPTS.miexpose.c+=	-Wno-error

.include <bsd.x11.mk>
.include <bsd.lib.mk>
