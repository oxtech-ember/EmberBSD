# $NetBSD: Makefile.rules.inc,v 1.11 2024/11/23 07:39:04 lukem Exp $

SRCS+= ${HEIMSRCS:N*.et:N*.in:N*.asn1}

# Text artifacts need the same source identities as compiler-generated output.
# Keep this recursive: bsd.sys.mk supplies REPRO_EXTERNAL_ROOTS later.
# Map external inputs first, including sources nested below NETBSDSRCDIR.
NORMALIZE_SRC= -e ''
.if ${MKREPRO:Uno} == "yes"
_HEIMDAL_REPRO_SPECIALS=[][)(^$$.?*+{}\\;]
NORMALIZE_SRC= ${REPRO_EXTERNAL_ROOTS:@_r@-e 's;${_r:C/=.*//:C/${_HEIMDAL_REPRO_SPECIALS}/\\\\&/g};${_r:C/^[^=]*=//};g'@}
NORMALIZE_SRC+= -e 's;${NETBSDSRCDIR:C/${_HEIMDAL_REPRO_SPECIALS}/\\\\&/g};/usr/src;g'
.endif
_HEIMDAL_REPRO_INPUTS= ${HEIMBASE}/Makefile.rules.inc \
    ${NETBSDSRCDIR}/share/mk/bsd.sys.mk

.for et_src in ${HEIMSRCS:M*.et}
CLEANFILES+=		${et_src:.et=.c} ${et_src:.et=.h}
CLEANFILES+=		${et_src:.et=.c.repro} ${et_src:.et=.h.repro}
DPSRCS+=		${et_src:.et=.c} ${et_src:.et=.h}
COMPILE_ET_INCS+=	${et_src:.et=.h}
OBJS+=			${et_src:.et=.o}

# Both targets write the pair; serialize them when requested together.
.ORDER: ${et_src:.et=.c} ${et_src:.et=.h}
${et_src:.et=.c} ${et_src:.et=.h}: ${et_src} ${TOOL_COMPILE_ET} ${_HEIMDAL_REPRO_INPUTS}
	${_MKTARGET_CREATE}
	${TOOL_COMPILE_ET} ${.ALLSRC:[1]}
.if ${MKREPRO:Uno} == "yes"
.for _et_out in ${et_src:.et=.c} ${et_src:.et=.h}
	${TOOL_SED} -E ${NORMALIZE_SRC} < ${_et_out} > ${_et_out}.repro
	mv -f ${_et_out}.repro ${_et_out}
.endfor
.endif

.endfor

.for src in ${HEIMSRCS:M*.asn1}
ASN1_FILES.${src}	?= asn1_${src:.asn1=_asn1.x}

.if ${ASN1_FILES.${src}:[\#]} == 1
ASN1_OPTS.${src} ?= --one-code-file
.endif

CLEANFILES+=				\
	${src:.asn1=_asn1_files}	\
	${src:.asn1=_asn1-template.c}	\
	${src:.asn1=_asn1-template.x}	\
	${ASN1_FILES.${src}}		\
	${ASN1_FILES.${src}:.x=.c}	\
	${src:.asn1=_asn1.h}		\
	${src:.asn1=_asn1.hx}		\
	${src:.asn1=_asn1-priv.h}	\
	${src:.asn1=_asn1-priv.hx}	\
	${src:.asn1=.ts}

ASN1_INCS += ${src:.asn1=_asn1.h}

DPSRCS +=	${src:.asn1=_asn1.h} ${src:.asn1=_asn1-priv.h}
OBJS +=		${ASN1_FILES.${src}:.x=.o}

${ASN1_FILES.${src}}: ${src:.asn1=.ts}

${src:.asn1=_asn1.h}: ${src:.asn1=.ts} ${_HEIMDAL_REPRO_INPUTS}
	${_MKTARGET_CREATE}
	@${TOOL_SED} -E ${NORMALIZE_SRC}				\
	    -e 's,#include <(.*)_asn1\.h>,#include <krb5/\1_asn1.h>,'	\
	    2> /dev/null < ${src:.asn1=_asn1.hx} > ${src:.asn1=_asn1.h}

${src:.asn1=_asn1-priv.h}: ${src:.asn1=.ts}
	${_MKTARGET_CREATE}
	@cmp -s ${src:.asn1=_asn1-priv.hx} ${src:.asn1=_asn1-priv.h}	\
	    2> /dev/null ||						\
	    cp ${src:.asn1=_asn1-priv.hx} ${src:.asn1=_asn1-priv.h}

.if exists(${src:.asn1=.opt})
${src:.asn1=.ts}: ${src} ${src:.asn1=.opt} ${TOOL_ASN1_COMPILE}
	${_MKTARGET_CREATE}
	${TOOL_ASN1_COMPILE}			\
		${ASN1_OPTS.${src}}		\
		--option-file=${.ALLSRC:[2]}	\
		${.ALLSRC:[1]} ${src:.asn1=_asn1}
	@touch $@
.else
${src:.asn1=.ts}: ${src} ${TOOL_ASN1_COMPILE}
	${_MKTARGET_CREATE}
	${TOOL_ASN1_COMPILE}			\
		${ASN1_OPTS.${src}}		\
		${.ALLSRC:[1]} ${src:.asn1=_asn1}
	@touch $@
.endif

.for x2c in ${ASN1_FILES.${src}}
${x2c:.x=.c}: ${x2c} ${_HEIMDAL_REPRO_INPUTS}
	${_MKTARGET_CREATE}
	@${TOOL_SED} -E ${NORMALIZE_SRC} < ${.ALLSRC:[1]} > ${x2c}.r
	@cmp -s ${x2c}.r ${x2c:.x=.c} 2> /dev/null || cp ${x2c}.r ${x2c:.x=.c}
	@rm -f ${x2c}.r
.endfor

.endfor

#
# and for slc:

.for slc_src in ${HEIMSRCS:M*.in}
CLEANFILES+=	${slc_src:.in=.c} ${slc_src:.in=.h}
DPSRCS+=	${slc_src:.in=.c} ${slc_src:.in=.h}
OBJS+=		${slc_src:.in=.o}
SLC_INCS+=	${slc_src:.in=.h}

${slc_src:.in=.c} ${slc_src:.in=.h}: ${slc_src}
	${_MKTARGET_CREATE}
	${TOOL_SLC} ${.ALLSRC}

.endfor

#
# And a tiny bit of logic for bsd.prog.mk:

.if defined(PROG) && defined(OBJS)
OBJS.${PROG} += ${OBJS}
.endif

.if defined(LIB) && exists(version-script.map) && !defined(NO_VERSION_SCRIPT)
version-script.map:

# XXX For libkadm5clnt, this should be version-script-client.map --
# requires major revbump, though; see PR lib/58795: libkadm5clnt.so is
# built without symbol version map.
VERSION_MAP=	version-script.map
.endif
