# $NetBSD: newvers.mk,v 1.4 2024/05/01 14:52:01 christos Exp $

MKREPRO?=no

.if ${MKREPRO} == "yes"
.	if ${MKREPRO_TIMESTAMP:U0} != 0
_NVFLAGS=${NVFLAGS} -r ${MKREPRO_TIMESTAMP} -i ${KERNEL_BUILD:T} -m ${MACHINE}
.	else
_NVFLAGS=${NVFLAGS} -R
.	endif
.else
_NVFLAGS=${NVFLAGS}
.endif

.if !target(vers.o)
# Version metadata can change while all compiled kernel inputs stay current.
# Force the version object on a content change, including within one second.
# Unchanged values preserve both the version object and the linked kernel.
EXTRA_CLEAN+= vers.inputs
_NV_INPUTS_CHANGED!= printf '%s\n' ${BUILDINFO:U:Q} ${_NVFLAGS:Q} | cmp -s - vers.inputs; echo $$?
.if ${_NV_INPUTS_CHANGED} != 0
.PHONY: vers.inputs vers.o
.endif
vers.inputs:
	@tmp=vers.inputs.tmp.$$$$; \
	    printf '%s\n' ${BUILDINFO:U:Q} ${_NVFLAGS:Q} > "$$tmp" && \
	    mv "$$tmp" vers.inputs
newvers: vers.o
vers.o: vers.inputs ${SYSTEM_OBJ:O} Makefile $S/conf/newvers.sh \
		$S/conf/osrelease.sh ${_NETBSD_VERSION_DEPENDS}
	${_MKMSG_CREATE} vers.c
	TOOL_AWK=${TOOL_AWK} TOOL_DATE=${TOOL_DATE} TOOL_HOSTNAME=${TOOL_HOSTNAME} \
	    ${HOST_SH} $S/conf/newvers.sh ${_NVFLAGS}
	${_MKTARGET_COMPILE}
	${CC} ${CFLAGS} ${CPPFLAGS} ${PROF} -c vers.c
	${COMPILE_CTFCONVERT}
.endif
