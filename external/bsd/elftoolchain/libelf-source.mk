# Use the generated Ports snapshot for every libelf/common consumer.
.if !defined(_EMBER_LIBELF_SOURCE_MK_)
_EMBER_LIBELF_SOURCE_MK_=1

.if !defined(EMBER_LIBELF_SOURCE)
EMBER_LIBELF_SOURCE:= ${.PARSEDIR:tA}/libelf-source
.endif
.if empty(EMBER_LIBELF_SOURCE) || empty(EMBER_LIBELF_SOURCE:M/*)
.error EMBER_LIBELF_SOURCE must be an absolute prepared source path
.endif
.if ${EMBER_LIBELF_SOURCE:[#]} != 1
.error EMBER_LIBELF_SOURCE must not contain whitespace; BSD .PATH cannot safely resolve source paths with whitespace
.endif

.for input in common/_elftc.h common/elfdefinitions.h common/sys/elfdefinitions.m4 common/sys/elfconstants.m4 libelf/elf.c libelf/libelf.h libelf/gelf.h libelf/elf_types.m4 libelf/libelf_convert.m4 ports.distinfo libelf-source.json
.if !exists(${EMBER_LIBELF_SOURCE}/${input})
.error Missing prepared libelf input: ${EMBER_LIBELF_SOURCE}/${input}
.endif
.endfor
.endif
