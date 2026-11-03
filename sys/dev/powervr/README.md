# PowerVR firmware metadata and MIPS preparation

<!-- Origin: EmberBSD PowerVR firmware preparation, AI-assisted. -->

This CPU-only layer parses bounded firmware metadata, prepares ELF image
buffers, and encodes MIPS page tables and boot data. It does not access
hardware, map DMA, start firmware, or provide graphics. The A733 platform
wrappers retain their existing strict feature policy and register address.
The platform supplies and validates the register physical address before
calling the common boot-data encoder. Before adding a hardware consumer,
the platform must establish the live register resource and verify owned RAM
ranges do not overlap MMIO; the encoder cannot establish either fact.

`PVR_GE8300_EXPERIMENTAL` selects an experimental preparation policy for
BVNC 22.102.54.38 and the exact `rogue_22.102.54.38_v1.fw` image from the
selected `powervr-firmware` source. Its SHA-256 is
`b571cdd90312c20fe87f14aae43484f7279859921ae3abe58e935412040c7f98`.
The matching image reports firmware ABI 1.1, build 6976702, six layouts,
and device array counts 1, 1, 2, and 22. A digest match identifies this
pinned input; it is not firmware authentication or hardware qualification.

Its raw BRN and ERN masks are `0x54000610` and `0x1a5`; feature words are
`0x9219dd57fdb6005b` and `0x4000000004`. Unknown BRN bits 26, 28 and 30,
ERN bit 8 and feature bit 102 remain visible in raw masks. They are not
claimed as implemented capabilities. The feature query accepts only feature
IDs known to the shared decoder. Unknown images, changed bytes, incompatible
BVNC and malformed buffers are rejected; errors leave output unchanged.

To exercise both actual firmware inputs and the GE CPU-only path, use the
existing `ember/tools/a733-gpu-fwinfo-contract.sh`,
`a733-gpu-fwimage-contract.sh` and `pvr-ge8300-fw-contract.sh` with the
selected host Clang and firmware files. Successful tests establish no GPU,
firmware execution, compute or rendering support.
