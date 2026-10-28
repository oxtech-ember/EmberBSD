#	Linker options shared by the i386 and amd64 boot programs.

.if !defined(STAND_LD_CREF)
# The boot programs are fixed-address images. GNU ld links them statically
# from their -Ttext address, but ld.lld adds a .interp section unless told
# -static and places the first section at its default image base 0x400000,
# above every load address used here. ld.lld also rejects the single-dash
# -cref spelling that GNU ld accepts.
.if !empty(LD_DRIVER_FLAGS:M--ld-path=*/ld.lld)
LD_DRIVER_FLAGS+=	-static -Wl,--image-base=0
STAND_LD_CREF=		-Wl,--cref
.else
STAND_LD_CREF=		-Wl,-cref
.endif
.endif
