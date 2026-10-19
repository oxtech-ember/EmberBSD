<!-- Origin: EmberBSD firmware framebuffer descriptor and console ownership. -->
# Firmware framebuffer contract

`simplefb` supplies a checked firmware framebuffer descriptor to a reserved
boot-console owner. It preserves the existing VirtGPU selection policy and
ordinary genfb attachment. This interface does not implement a KMS device.

## Descriptor

[The public header](../../sys/dev/fdt/simplefbvar.h) defines the descriptor.
[The driver](../../sys/dev/fdt/simplefb.c) reads it once during attachment.
It includes the bus tag, physical address, aperture length, visible byte span,
width, height, stride, depth and exact firmware pixel format.

The reader accepts the existing six formats: `a8b8g8r8`, `x8r8g8b8`,
`r8g8b8a8`, `b8g8r8x8`, `x2r10g10b10` and `r5g6b5`.
It preserves the existing genfb swap and ten-bit flags.
A consumer must select the formats that it can present correctly.

The reader rejects missing properties, unknown formats and zero dimensions.
Each row must fit its stride. The full visible span must fit the aperture.
Dimensions, stride and visible span must also fit the existing signed genfb
arithmetic. The physical address must fit `bus_addr_t`.
The aperture end and its page rounding must not wrap.
A nonzero MD `simplefb-physaddr` property overrides the firmware address.
Genfb receives the checked address and stride without narrowing them.

A failed query leaves the caller's output unchanged. A successful query copies
an immutable snapshot. It does not map memory, change the display mode, or
authorize access without the ownership sequence below.

## Boot-console ownership

The current selection policy defers the firmware console only when
`VIRTGPU_CONSOLE` is enabled. Other configurations attach genfb immediately.
This patch does not add a new selection policy for physical KMS devices.

1. Call `simplefb_console_reserve(owner)` before `config_finalize` ends.
   The owner must be non-NULL. Only one reservation can exist.
2. Call `simplefb_console_get_framebuffer(owner, &descriptor)` if the consumer
   needs the existing firmware surface. Only the current uncommitted owner can
   obtain the snapshot.
3. Prepare the replacement console while the reservation remains uncommitted.
   Retain the descriptor copy if it will be needed after commit.
4. On a preparation failure, stop using the surface and release all prepared
   mappings before `simplefb_console_release(owner)`.
5. Commit only after successful preparation with `simplefb_console_commit(owner)`.
   Commit preserves the existing VirtGPU API and prevents the genfb fallback.

Reservation does not require a valid firmware descriptor. VirtGPU creates its
own scanout and can replace an unreadable firmware surface.
Descriptor queries still return the recorded validation error.
NULL, foreign and repeated ownership operations cannot transfer ownership.
An invalid commit asserts in diagnostic kernels and leaves state unchanged.

The exact deferred simplefb instance has one finalizer. If no owner commits,
the finalizer attaches genfb once. A released reservation can be claimed again
before finalization. A committed reservation cannot be released or queried.

This is a serialized boot-attachment interface. It does not arbitrate runtime
DRM master, wsdisplay modes, console drawing, hotplug or panic recovery.
The caller must not leave a competing writer active after fallback.

## Source checks

Run the actual-source host contract and the existing VirtGPU console contract:

```sh
ruby ember/tools/simplefb-contract.rb
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    ruby ember/tools/simplefb-contract.rb
sh ember/tools/virtgpu-console-contract.sh
sh ember/tests/build-kernel.sh
```

The new runner compiles the actual parser, attachment and ownership functions.
It covers ordinary attachment, deferred selection and multi-console selection.
The firmware, property, bus-map and genfb services are controlled adapters.
Seven baseline cases reject narrowed addresses and strides, malformed geometry,
address overflow and NULL ownership. Seven source mutants verify format,
query, release, commit and exactly-once fallback guards.
The kernel contract dispatcher runs this test in both modes.

Baseline and changed translation units pass GCC 16 path analysis and Clang
Static Analyzer with the retained AArch64 kernel headers and compile flags.
The extra deferred variant defines `VIRTGPU_CONSOLE` for that translation unit.
Cppcheck reports the same seven existing style findings in both versions.
No categories are suppressed. Host ASan/UBSan checks pass.
Valgrind Memcheck is unavailable on the macOS/arm64 host.

These are source and translation-unit checks. They do not establish a linked
kernel, physical display output or an accelerated Plasma session.
A fixed-mode KMS consumer still needs GEM buffers, atomic presentation,
console recovery and integration with the separate V3D render device.
The descriptor provides no vblank, mode switching, PRIME import or acceleration.

The optional [early output reader hooks](early-framebuffer-readers.md) cover
actual boot binding and replay readers. They provide no close, detach, ownership
or firmware handoff operation.
