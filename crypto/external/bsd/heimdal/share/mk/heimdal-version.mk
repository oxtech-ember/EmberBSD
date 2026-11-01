# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.

# Heimdal's configure script creates version.h with local host and date data.
# Keep the selected source identity, but generate the header in this object dir.
CPPFLAGS:= -I${.OBJDIR} ${CPPFLAGS}
HOST_CPPFLAGS:= -I${.OBJDIR} ${HOST_CPPFLAGS}

.NOPATH: version.h

version.h: ${HEIMDIST}/configure ${HEIMBASE}/share/mk/heimdal-version.awk
	${TOOL_AWK} -f ${HEIMBASE}/share/mk/heimdal-version.awk ${HEIMDIST}/configure > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}

CLEANFILES+= version.h version.h.tmp

.if ${HEIMDAL_PRINT_VERSION:Uno} == "yes"
DPSRCS+= print_version.h

make-print-version.lo: version.h

make-print-version: make-print-version.lo
	${HOST_LINK.c} -o ${.TARGET} ${.ALLSRC}

print_version.h: make-print-version
	${_MKTARGET_CREATE}
	./make-print-version print_version.h

print_version.o print_version.ln: version.h print_version.h

.NOPATH: print_version.h make-print-version

CLEANFILES+= print_version.h make-print-version make-print-version.lo
.endif
