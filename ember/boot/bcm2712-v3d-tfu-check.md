# Finite native TFU raster and tiled gate

`ember/tools/bcm2712-v3d-tfu-check.c` submits real native DRM TFU jobs.
It reuses the existing GEM allocation, handle-release and pattern helpers.
The original `allocate(fd)` still requests 20 KiB. Its new internal
`allocate_bytes(fd, size)` helper permits this checker to use 24 KiB BOs.
This changes no kernel or public userspace ABI.

## Cases and oracle

The CLI takes one render node. It retains two distinct 24 KiB BOs throughout
the run: 49,152 requested live bytes, excluding metadata or page rounding.
The complete source BO is filled with a different deterministic pattern for
each job. The destination starts with per-word guard values.

| Case | Active size | Stride | Source offset | Destination offset |
| --- | --- | --- | --- | --- |
| Existing raster shape | 64x64 | 64 pixels / 256 bytes | 0 | 0 |
| Interior larger shape | 84x52 | 96 pixels / 384 bytes | 256 bytes | 512 bytes |

Each case first submits four jobs with user ICFG.IOC=1, then four with IOC=0.
The raster phase submits 16 jobs and checks 67,712 active 32-bit pixels.
The second case reaches active destination bytes beyond 16 KiB. Its declared
pitched destination region ends at byte 20,480, leaving a 4 KiB outer guard.
Its stride and offsets are explicit fixtures, not a claim about all Mesa
resource alignment. The register encoding is raster/unfiltered R32F from
Mesa 26.2.4 `v3d_tfu.h` and the texture-format enum in `v3d_packet.xml`.

The oracle calculates active source and destination word positions from the
separate offsets, row stride and dimensions. It requires the entire source
BO to remain unchanged. It checks every active destination pixel and every
destination word outside the declared pitched region. It permits writes to
intra-row destination padding. It does not infer undocumented prefetch guards.
There is no silent fallback, software copy or alternate renderer.
The independent UIF cases below add a pinned Mesa CPU tiling oracle.
This gate does not implement a mipmap oracle.

## Independent UIF transfers

The gate then submits four raster-to-UIF_NO_XOR and four UIF_NO_XOR-to-raster
jobs. The complete gate requires **24 jobs and 100,480 checked pixels**.
A partial 16-job raster result is not a full PASS. Both images are 64x64 R32F,
with a 4096-byte offset and no tile padding. Each 16 KiB image has a 4 KiB
outer guard on both sides of its 24 KiB BO. Live GPU requests remain 48 KiB.
A 48 KiB CPU reference allocation stores expected output and unchanged input.

The raster stride is 64 pixels. The UIF height is 64 pixels, so its register
stride is `64 / (2 * v3d_utile_height(4)) = 8` UIF blocks. Input/output format
values and shifts come from the pinned `v3d_tfu.h`. IOC is zero in both tiled
directions. NUMMM and DIMTW remain zero. This phase does not generate mipmaps.

Before each forward job, the CPU fills a nonuniform raster pattern and fills
the tiled destination with guards. It builds expected tiled bytes through
unchanged Mesa scalar CPU offset helpers. It verifies that these offsets form
a bounded, aligned bijection for this exact shape. After TFU completes, it
compares the intermediate tiled BO directly with that reference.

Before each reverse job, the CPU fills the tiled source with a different
pattern. It compares the raster output with an independent expected result.
Both directions check the complete source and the destination outer guards.
A symmetric roundtrip alone cannot pass. A raw memcpy in either direction
must fail because raster and UIF byte positions differ for this shape.

The CPU oracle is generated at build time from Mesa 26.2.4 sources. The
[generator](../tools/bcm2712-v3d-tfu-oracle.rb) accepts a source directory with
exact file hashes or the public upstream archive:

- URL: <https://archive.mesa3d.org/mesa-26.2.4.tar.xz>
- Archive SHA256: `bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9`
- `src/broadcom/common/v3d_tiling.c`: `b481709b5bf5c6f7609bb7c49d1dd66098ccddf955fd8fe3d3f10c2f4ee5f4f3`
- `src/broadcom/common/v3d_tfu.h`: `7167dd4b1dadb5d1b37f7ba575093d713c52f2fbdb3d0a5482658139c57e1a59`

The generated header retains the upstream licenses and the unchanged bodies
of five scalar utile/UIF helpers. Its explicit integer/compiler services
supply alignment, `ffs` and unsupported-branch termination. They do not supply
a second tiling implementation. Input hashes and extraction boundaries fail
closed. No Mesa runtime library or target Mesa build is required.

## Synchronization and failure

One file-owned binary syncobj starts signaled. Every submit uses it as both
input and output. After submission, the checker waits on an absolute
CLOCK_MONOTONIC deadline one second ahead, then checks WAIT_BO for both BOs.
It closes both GEM handles, unmaps both BOs and checks descriptor close.
Descriptor close releases the binary syncobj.

The first four 64x64/IOC1 jobs preserve the old accepted payload. An older
fixed-payload kernel rejects IOC0 with EINVAL. That is a negative baseline,
not success. A full PASS requires all raster and independent tiled cases, both raster IOC values, pixels,
source/outer guards, fences and cleanup. An ioctl, memory or pixel failure
exits nonzero. Process exit releases residual descriptors and mappings.

The job count and fence waits are finite. A blocked kernel ioctl has no
userspace alarm in this program. Use an external bounded controller for
physical execution. Record the exact kernel/checker revisions, SHA256,
installed runtime identities, elapsed time, temperature and native memory/error
counters. Native BO reservations/releases must return to the initial total.
Closing userspace handles alone does not prove PTE revocation or BO return.

## Cross build and execution

Use the common EmberBSD GCC16 compiler and recorded fork sysroot. Select its
configured runtime paths and linker as for other native DRM checkers.
Use the DRM UAPI headers from the matching OS source:

```sh
mkdir "$ORACLE_WORK"
ruby "$SOURCE/ember/tools/bcm2712-v3d-tfu-oracle.rb" "$MESA_SOURCE_OR_ARCHIVE" \
    "$ORACLE_WORK/mesa-tfu-oracle.h"
"$CC" --sysroot="$SYSROOT" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -Wno-unused-function -pthread -I"$ORACLE_WORK" \
    -I"$SOURCE/sys/external/bsd/drm2/dist/include/uapi/drm" \
    "$SOURCE/ember/tools/bcm2712-v3d-tfu-check.c" -o "$OUTPUT"
```

The unused-function option matches the existing shared-checker build: imported
legacy TFU cases are retained but are not called by this gate. The checker
needs libc/pthread and DRM UAPI headers; it does not link Mesa or libdrm.
Verify the actual ELF interpreter, dependencies, compiler and runtime inputs.
Build on the development host. Execute the resulting program on EmberBSD:

```sh
./tfu-check /dev/dri/renderD128
```

No physical acceptance of this gate follows from compilation.
`DRM_V3D_PARAM_SUPPORTS_TFU` remains unchanged. With capability zero, Mesa
does not select its TFU mipmap callback. A successful `glGenerateMipmap`
readback through a fallback path would not qualify TFU mipmap execution.

## Actual-source host contract

```sh
ruby ember/tools/bcm2712-v3d-tfu-check-contract.rb --source /absolute/source \
    --mesa-source /absolute/mesa-26.2.4-source-or-archive
```

The runner compiles the production checker and a byte-identical snapshot of
its shared helpers. Controlled DRM services validate exact geometry, offsets,
IOC order, binary deadlines, WAIT_BO calls, live memory and cleanup. They test
legacy 20 KiB allocation and the new 24 KiB requests. An explicit padding case
must pass. Missing copy, truncated 16 KiB mappings, changed source/guards,
allocation/map/fence/close errors and the old IOC restriction must fail.
Independent tiled models reject NOP, raw memcpy, stale input, source/guard
corruption and a linear caller oracle. Compiled controls require every tiled
phase and intermediate/source checks. They require exact expected exit codes
and causal diagnostics.
Signals, sanitizer diagnostics and timeouts cannot count as expected failures.
The timeout controller kills and waits for its own process group.
Host models and sanitizers do not execute TFU or qualify physical DMA behavior.
