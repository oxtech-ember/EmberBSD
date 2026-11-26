# Shared native OS update parsers. jsmn comes from the base source tree.
EUDIR=${NETBSDSRCDIR}/common/lib/libemberupdate
JSMNDIR=${NETBSDSRCDIR}/external/bsd/ntp/dist/libjsmn
.PATH: ${EUDIR} ${JSMNDIR}
CPPFLAGS+= -I${EUDIR} -I${JSMNDIR} -DJSMN_PARENT_LINKS
SRCS+= json.c jsmn.c
