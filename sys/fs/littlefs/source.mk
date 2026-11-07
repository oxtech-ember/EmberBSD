# Common littlefs source; shared by kernel and native makefs consumers.
.if !defined(_EMBER_LITTLEFS_SOURCE_MK_)
_EMBER_LITTLEFS_SOURCE_MK_=1
.if !defined(EMBER_LITTLEFS_SOURCE) || empty(EMBER_LITTLEFS_SOURCE)
.error Set EMBER_LITTLEFS_SOURCE to the prepared common Ports source
.elif empty(EMBER_LITTLEFS_SOURCE:M/*)
.error EMBER_LITTLEFS_SOURCE must be an absolute path
.endif
.for input in lfs.c lfs.h lfs_util.c lfs_util.h LICENSE.md emberbsd-prepared-source.json
.if !exists(${EMBER_LITTLEFS_SOURCE}/${input})
.error Missing prepared littlefs input: ${EMBER_LITTLEFS_SOURCE}/${input}
.endif
.endfor
.endif
