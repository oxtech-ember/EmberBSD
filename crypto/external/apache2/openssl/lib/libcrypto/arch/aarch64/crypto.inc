.PATH.S: ${.PARSEDIR}
CPUID_SRCS += arm64cpuid.S armcap.c
CPUID = yes
ARM_MAX_ARCH=8
CPPFLAGS += -D__ARM_MAX_ARCH__=${ARM_MAX_ARCH}
CPPFLAGS += -DOPENSSL_CPUID_OBJ
# EmberBSD: the Clang integrated assembler builds these sources.  With
# -no-integrated-as, Clang on a Darwin host runs Apple as on ELF assembly.
.include "../../crypto.inc"

