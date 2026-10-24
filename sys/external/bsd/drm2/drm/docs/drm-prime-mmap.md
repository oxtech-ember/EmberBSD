# Native AMDGPU PRIME mmap

Native AMDGPU DMA-BUF mappings use the existing TTM/UVM pager. The exporter
rejects userptr and `AMDGPU_GEM_CREATE_NO_CPU_ACCESS` buffers. It checks the
requested range against the BO size before adding the fake DRM offset.
The generic PRIME helper converts the VMA node start from pages to bytes and
rejects an offset that cannot fit in native `off_t`.

The PRIME descriptor authorizes access to its specific exported object.
The generic helper grants a temporary DRM-file tag, calls the normal driver
mapping callback, and revokes the tag. TTM transfers one BO reference to UVM;
its pager reference and detach hooks retain and release that reference.
Normal DRM-file ownership checks and render-node `MAP_DUMB` restrictions stay
in effect. The native DMA-BUF file operation limits mmap and later mprotect
access to the descriptor's read/write rights. It rejects executable mappings.

## Hardware acceptance

Build the libc-only probe with the shared GCC16 compiler and a matching
EmberBSD userland sysroot. The headers must come from the tested OS revision:

```sh
"$CC" --sysroot="$SYSROOT" -O2 -static -std=c11 -D_NETBSD_SOURCE \
    -Wall -Wextra -Werror \
    -I"$SOURCE/sys/external/bsd/drm2/dist/include/uapi" \
    "$SOURCE/ember/tools/drm-prime-mmap-check.c" -o "$OUTPUT"
"$OUTPUT" /dev/dri/card0
```

Select the primary DRM node of the intended AMDGPU device. Run as both root
and an ordinary user with access to that node. The probe creates private dumb
buffers and has a 30-second alarm. It does not become DRM master, change a
display mode or submit a GPU job.

The probe checks RW and independently exported RO objects. It checks shared
patterns, a nonzero offset, the final page, range failures, descriptor rights,
mprotect limits and foreign-file isolation before and after PRIME mapping.
It closes every descriptor, GEM handle and other mapping before checking the
remaining PRIME mapping and its forked lifetime. A failure returns nonzero.
The unimplemented exporter fails at the first PRIME mmap with `ENODEV`.

This acceptance covers CPU mappings of dumb buffers. It does not prove GPU
fences, DMA cache maintenance, foreign-device import, scanout, Mesa or a desktop
session. Those require separate tests. This repair and probe are AI-assisted
EmberBSD work. Imported licenses and authorship remain in their source files.
