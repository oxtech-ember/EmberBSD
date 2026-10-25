#	$NetBSD: Makefile,v 1.1 2003/03/07 01:14:30 thorpej Exp $

HOSTPROGNAME=	${_TOOL_PREFIX}hexdump
HOST_SRCDIR=	usr.bin/hexdump

.include "${.CURDIR}/../Makefile.host"

# Origin: EmberBSD (AI-assisted). Install the existing od invocation on hosts.
# Use the host symlink installer, as tools/llvm-clang does for its aliases.
install.host: ${HOST_BINDIR}/${_TOOL_PREFIX}od${HOSTEXEEXT}

${HOST_BINDIR}/${_TOOL_PREFIX}od${HOSTEXEEXT}: ${HOST_INSTALLPROG}
	${_MKTARGET_INSTALL}
	mkdir -p ${HOST_BINDIR}
	${HOST_INSTALL_SYMLINK} ${HOSTPROGNAME}${HOSTEXEEXT} ${.TARGET}
