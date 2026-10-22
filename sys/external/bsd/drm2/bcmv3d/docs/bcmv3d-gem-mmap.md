# Native V3D GEM prefix mapping

`bcm2712-v3d-mmap-check` tests CPU mappings of native GEM objects. It submits
no GPU jobs, display state, page flips or MMIO operations. It opens only the
explicit DRM node and requires the native `v3d` driver name.

The test allocates two objects of three and five host pages. It fills each
complete mapping with a distinct nonzero byte pattern. It maps one-page and
two-page prefixes at the exact native object offset. Each READ mapping must
match every byte. Each RW mapping changes the first byte, last byte and page
boundary. The full mapping must observe those changes. Writes through the
full mapping must also appear in the prefix. All remaining bytes and the
other object must retain their patterns.

A third 16 MiB object uses the same checks with an 8,294,400-byte prefix.
That prefix equals a 1920x1080 XRGB8888 image. The test does not assume that
an image's byte count equals its backing object's size. The operating system
rounds mmap lengths to host pages. This is a CPU mapping contract, including
the 64 KiB host-page model. It establishes no GPU support for 64 KiB pages.

Unknown, oversized and interior-offset mappings must fail with `EINVAL`.
An independent DRM file without the object's handle must fail with `EACCES`.
An anonymous zero mapping is a failure. This contract requires the native
strict GEM mapper; it does not describe other DRM drivers' legacy mappings.
The test also closes all handles and the owner file before its final alias
check. Live mappings must retain the objects until the final `munmap`.

Build a clean source commit with the recorded common EmberBSD GCC16 and
sysroot. Record the actual invoked linker, its version, headers and CRT.
Use the selected common Binutils 2.47 for hardware acceptance. A compiler's
`-print-prog-name` output alone does not prove which linker ran.

```sh
CC=/absolute/common/bin/aarch64--netbsd-gcc \
SYSROOT=/absolute/recorded-emberbsd-sysroot \
    sh ember/tools/bcm2712-v3d-mmap-check-build.sh /absolute/source /absolute/new-output
```

Run the executable as the authorized device user under a finite external
process deadline. It needs no DRM master or running compositor. For example:

```sh
timeout -k 2 30 ./bcm2712-v3d-mmap-check /dev/dri/renderD128
```

All detected failures return nonzero. Cleanup attempts every acquired map,
handle and file. Preserve the executable hash and complete output. Record BO
accounting before and after the run. A source contract or compilation is not
hardware acceptance. Use the same executable for a known negative kernel and
the corrected kernel when testing the short-mapping regression.

On physical CM5 with 4 KiB host pages, the checker from `7f8c1ed1`
(ELF SHA256
`f0f2cbcab56a639ae4d5d776ab2a7b990f6aa017716559141020472042cc632c`)
failed on kernel `cff738af` at the first prefix byte: zero instead of the
nonzero full-map pattern. The same executable passed on kernel `12b95ff7`:
all five READ/RW prefix cases, whole-tail and other-object guards, exact
`EINVAL` and `EACCES` failures, and mappings retained after owner close.
This includes the 8,294,400-byte prefix of the 16 MiB object. All three BOs
were released, with reserved memory returned to 12,500,992 bytes and no new
allocation or execution fault. This accepts the tested CPU mapping contract.
It does not accept GPU rendering, HDMI output or physical 64 KiB mappings.

The controlled host contract compiles the actual caller body with modeled
DRM and mapping services. It covers allocation failures, distinct object
patterns, anonymous and private-copy mappings, tail changes, exact errors,
mapping lifetime and cleanup. Named compiled controls and process controls
must fail at their exact expected boundary. The model does not execute a
native kernel or establish physical mapping, cache or display behavior.

```sh
ruby ember/tools/bcm2712-v3d-mmap-check-contract.rb /absolute/source
```

Native `V3D_MMAP_BO` and `MODE_MAP_DUMB` serialize first offset creation
with the same device `struct_mutex`. Each caller holds a lookup reference
until publication and reads the offset before it unlocks. Each caller drops
that reference after it unlocks. The generic and legacy APIs are unchanged.
The source contract pauses actual VMA allocation and insertion. It checks
concurrent callers, alias handles, exact allocation/release counts, errors
and closing the last handles during publication. These host checks do not
execute a native kernel or test display output.

```sh
ruby ember/tools/bcmv3d-mmap-offset-contract.rb --work NEW_DIRECTORY
```

Kernel `8c26cd72` also passed the same five CPU mapping cases on physical
CM5 with 4 KiB pages. The upstream `modetest` atomic route completed one
1920x1080 mode set and normal teardown on that kernel. It created and
released one BO. Reserved memory returned to 12,500,992 bytes. The cached
owner recorded two latches and no owner or IRQ error. These functional
target checks do not force concurrent first offset creation. They do not
accept physical HDMI pixels, input, a seat provider or 64 KiB target pages.
