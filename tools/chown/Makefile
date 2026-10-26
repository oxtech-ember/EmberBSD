# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted). Canonical source and common compatibility.

HOSTPROGNAME=	${_TOOL_PREFIX}chown
HOST_SRCDIR=	sbin/chown

.include "${.CURDIR}/../Makefile.host"

# Install the same image under the group-only invocation name.
_HOST_CHGRP=	${HOST_BINDIR}/${_TOOL_PREFIX}chgrp${HOSTEXEEXT}
install.host: ${_HOST_CHGRP}
${_HOST_CHGRP}:: ${HOST_INSTALLPROG}
	${_MKTARGET_INSTALL}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME}${HOSTEXEEXT} ${.TARGET}
