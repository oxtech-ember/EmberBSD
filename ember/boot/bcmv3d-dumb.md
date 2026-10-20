# Native V3D dumb buffers

`bcmv3d_dumb_create` implements the DRM dumb-buffer creation callback through
the native V3D GEM allocator. It creates a CPU-written buffer with a native
handle and native lifetime accounting. The separate declaration is in
`bcmv3d_dumb.h`. Driver registration belongs to the KMS integration; this
callback alone does not enable `DRIVER_MODESET` or create display objects.

The callback accepts nonzero width and height, 32 bits per pixel and zero flags.
It calculates a 64-byte-aligned pitch with 64-bit arithmetic. It rejects product
overflow, a pitch that cannot fit the 32-bit DRM output and a pixel allocation
over the native 256 MiB BO ceiling. The remaining device quota can reject a
smaller allocation with `ENOMEM`. Handle, pitch and size outputs stay zero on
failure. Input dimensions and format fields stay unchanged. Errors use the DRM
convention: zero on success and a negative errno on failure.

The native allocator rounds storage to the actual kernel page size. The returned
size is the actual GEM object size. A 1920x1080 buffer has pitch 7680 and pixel
size 8294400. Its storage is 8294400 bytes with 4 KiB pages and 8323072 bytes with
64 KiB pages. Padding and all native BO storage are initialized by the allocator.
The callback uses native scattered-page allocation, as the existing V3D BO
ioctl does. It does not introduce a foreign CMA object or allocator.

A successful handle holds its own GEM reference. The callback releases the
constructor reference exactly once after handle creation, including handle
failure. Native allocation failures propagate their original negative errno.
Handle failures update the native BO failure accounting. The existing generic
DRM GEM dumb-map and dumb-destroy paths provide the normal handle operations;
they do not require a second callback implementation.

Dumb objects do not advertise GPU rendering. V3D and Mesa use the existing
hardware-specific BO ioctl for accelerated allocations. A successful dumb
allocation does not establish scanout, DMA retirement, KMS, page-flip events
or an accelerated desktop. Those consumers retain their own validation rules.

## Source contract

```sh
ruby ember/tools/bcmv3d-dumb-contract.rb
CC=cc CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
  DUMB_SKIP_MUTANTS=1 ruby ember/tools/bcmv3d-dumb-contract.rb
sh ember/tools/bcm2712-v3d-bo-contract.sh
ruby ember/tools/bcm2712-v3d-bo-stats-contract.rb
```

The new contract executes the actual callback and complete native allocator
with controlled kernel, DMA and DRM handle services. It takes the callback
argument from the actual DRM UAPI. It checks pitch boundaries, the inclusive
256 MiB ceiling, overflow refusal, 4 KiB and 64 KiB pages, scattered allocation,
allocation and handle failures, original errno signs, output clearing and
balanced constructor/handle references. Compiled callback controls must fail
through a contract assertion. Crashes and timeouts are not valid rejections.

`DUMB_EVIDENCE` requires a fresh directory and retains source hashes, exact
commands and outputs. `CC`, `CFLAGS` and `TEST_RUNNER` select the host compiler,
flags and runner. `DUMB_SKIP_MUTANTS=1` selects the positive memory check.
Host contracts and a target TU compile do not establish hardware acceptance.
