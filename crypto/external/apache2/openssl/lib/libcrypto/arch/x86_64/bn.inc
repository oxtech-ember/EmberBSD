.PATH.S: ${.PARSEDIR}
.PATH.c: ${OPENSSLSRC}/crypto/bn/asm
BN_SRCS = x86_64-gf2m.S x86_64-mont.S x86_64-mont5.S x86_64-gcc.c
BN_SRCS += \
rsaz-2k-avx512.S \
rsaz-2k-avxifma.S \
rsaz-3k-avx512.S \
rsaz-3k-avxifma.S \
rsaz-4k-avx512.S \
rsaz-4k-avxifma.S \
rsaz-avx2.S \
rsaz-x86_64.S \

# These files once needed external gas on older clang; current LLVM
# integrated as assembles them. Keep -fintegrated-as so Darwin hosts do
# not invoke Apple as via -fno-integrated-as.
.if ${HAVE_LLVM:U} == "yes"
COPTS.rsaz-2k-avx512.S  += -fintegrated-as
COPTS.rsaz-2k-avxifma.S += -fintegrated-as
COPTS.rsaz-3k-avx512.S  += -fintegrated-as
COPTS.rsaz-3k-avxifma.S += -fintegrated-as
COPTS.rsaz-4k-avx512.S  += -fintegrated-as
COPTS.rsaz-4k-avxifma.S += -fintegrated-as
.endif

CPPFLAGS += -DOPENSSL_BN_ASM_MONT -DOPENSSL_BN_ASM_MONT5 -DOPENSSL_BN_ASM_GF2m
.include "../../bn.inc"
