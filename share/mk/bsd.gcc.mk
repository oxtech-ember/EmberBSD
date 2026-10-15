#	$NetBSD: bsd.gcc.mk,v 1.12 2018/05/09 13:18:53 joerg Exp $

.if !defined(_BSD_GCC_MK_)
_BSD_GCC_MK_=1

# An audited Ports runtime can support an external GCC without installing
# its headers or development libraries into the OS destination tree.
.if defined(EMBER_EXTERNAL_GCC_RUNTIME) && !defined(HOSTPROG) && !defined(HOSTLIB)
.if empty(EMBER_EXTERNAL_GCC_RUNTIME:M/*) || \
    !empty(EMBER_EXTERNAL_GCC_RUNTIME:C,[A-Za-z0-9_./-],,g)
.error EMBER_EXTERNAL_GCC_RUNTIME must be an absolute path without whitespace or shell metacharacters
.endif
.if empty(EXTERNAL_TOOLCHAIN:M/*) || ${USETOOLS} != "yes" || ${ACTIVE_CC} != "gcc" || \
    ${ACTIVE_CXX} != "gcc" || ${MKGCC} != "no"
.error EMBER_EXTERNAL_GCC_RUNTIME requires the external GCC compiler with MKGCC=no
.endif
.if !empty(MLIBDIR:U) || \
    !((${MACHINE_ARCH} == "x86_64" && ${MACHINE_GNU_PLATFORM} == "x86_64--netbsd") || \
      (${MACHINE_ARCH} == "aarch64" && ${MACHINE_GNU_PLATFORM} == "aarch64--netbsd"))
.error EMBER_EXTERNAL_GCC_RUNTIME supports matching amd64 or aarch64 without multilib
.endif
_EMBER_GCC_CRTDIR=${EMBER_EXTERNAL_GCC_RUNTIME}/lib/gcc/${MACHINE_GNU_PLATFORM}/16.2.0
.for _ember_gcc_file in crtbegin.o crtbeginS.o crtend.o crtendS.o libgcc.a libgcc_eh.a
.if !exists(${_EMBER_GCC_CRTDIR}/${_ember_gcc_file})
.error EMBER_EXTERNAL_GCC_RUNTIME is missing ${_EMBER_GCC_CRTDIR}/${_ember_gcc_file}
.endif
.endfor
.for _ember_gcc_file in lib/libgcc_s.so.1 lib/libstdc++.so lib/libstdc++.so.7 \
    lib/libstdc++.a lib/libsupc++.a \
    include/c++/vector include/c++/${MACHINE_GNU_PLATFORM}/bits/c++config.h
.if !exists(${EMBER_EXTERNAL_GCC_RUNTIME}/${_ember_gcc_file})
.error EMBER_EXTERNAL_GCC_RUNTIME is missing ${EMBER_EXTERNAL_GCC_RUNTIME}/${_ember_gcc_file}
.endif
.endfor

_GCC_CRTBEGIN?=	${_EMBER_GCC_CRTDIR}/crtbegin.o
_GCC_CRTBEGINS?=	${_EMBER_GCC_CRTDIR}/crtbeginS.o
_GCC_CRTEND?=	${_EMBER_GCC_CRTDIR}/crtend.o
_GCC_CRTENDS?=	${_EMBER_GCC_CRTDIR}/crtendS.o
_GCC_LIBGCCDIR?=	${_EMBER_GCC_CRTDIR}
# bsd.prog.mk uses these names for explicit archive dependencies.
LIBSTDC++?=	${EMBER_EXTERNAL_GCC_RUNTIME}/lib/libstdc++.a
LIBSUPC++?=	${EMBER_EXTERNAL_GCC_RUNTIME}/lib/libsupc++.a

# Program links do not pass CXXFLAGS. Shared-library rules also depend on
# explicit CRT paths. Both paths must select the same GCC runtime.
CPPFLAGS+=	-B${_EMBER_GCC_CRTDIR}/
CXXFLAGS+=	-nostdinc++ -isystem ${EMBER_EXTERNAL_GCC_RUNTIME}/include/c++ \
		-isystem ${EMBER_EXTERNAL_GCC_RUNTIME}/include/c++/${MACHINE_GNU_PLATFORM} \
		-isystem ${EMBER_EXTERNAL_GCC_RUNTIME}/include/c++/backward
LDFLAGS+=	-B${_EMBER_GCC_CRTDIR}/ -L${_GCC_LIBGCCDIR} \
		-L${EMBER_EXTERNAL_GCC_RUNTIME}/lib -Wl,-rpath,/usr/pkg/gcc16/lib
.endif

_GCC_CRTBEGIN?=		${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crtbegin.o
_GCC_CRTBEGINS?=	${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crtbeginS.o
_GCC_CRTEND?=		${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crtend.o
_GCC_CRTENDS?=		${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crtendS.o
_GCC_CRTI?=		${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crti.o
_GCC_CRTN?=		${DESTDIR}/usr/lib/${MLIBDIR:D${MLIBDIR}/}crtn.o
_GCC_CRTDIR?=		${DESTDIR}/usr/lib${MLIBDIR:D/${MLIBDIR}}
_GCC_LIBGCCDIR?=	${DESTDIR}/usr/lib${MLIBDIR:D/${MLIBDIR}}

.endif	# ! defined(_BSD_GCC_MK_)
