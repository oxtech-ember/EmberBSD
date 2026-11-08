# SPDX-License-Identifier: BSD-2-Clause
.include <bsd.own.mk>

PROG= mount_littlefs
SRCS= mount_littlefs.c pathadj.c
MAN= mount_littlefs.8
MOUNT= ${NETBSDSRCDIR}/sbin/mount
CPPFLAGS+= -I${MOUNT}
.PATH: ${MOUNT}
DPADD+= ${LIBUTIL}
LDADD+= -lutil

.include <bsd.prog.mk>
