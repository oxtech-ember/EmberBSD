# Finite native TFU raster gate

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
The complete run submits 16 jobs and checks 67,712 active 32-bit pixels.
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
This phase does not implement a tiled or mipmap oracle. A later tiled gate
must use the pinned Mesa CPU tiling helpers with their licenses and origins.

## Synchronization and failure

One file-owned binary syncobj starts signaled. Every submit uses it as both
input and output. After submission, the checker waits on an absolute
CLOCK_MONOTONIC deadline one second ahead, then checks WAIT_BO for both BOs.
It closes both GEM handles, unmaps both BOs and checks descriptor close.
Descriptor close releases the binary syncobj.

The first four 64x64/IOC1 jobs preserve the old accepted payload. An older
fixed-payload kernel rejects IOC0 with EINVAL. That is a negative baseline,
not success. A full PASS requires every case, both IOC values, pixels,
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
"$CC" --sysroot="$SYSROOT" -std=c11 -O2 -g -Wall -Wextra -Werror \
    -Wno-unused-function -pthread \
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

No physical acceptance of this new gate follows from compilation.

## Actual-source host contract

```sh
ruby ember/tools/bcm2712-v3d-tfu-check-contract.rb --source /absolute/source
```

The runner compiles the production checker and a byte-identical snapshot of
its shared helpers. Controlled DRM services validate exact geometry, offsets,
IOC order, binary deadlines, WAIT_BO calls, live memory and cleanup. They test
legacy 20 KiB allocation and the new 24 KiB requests. An explicit padding case
must pass. Missing copy, truncated 16 KiB mappings, changed source/guards,
allocation/map/fence/close errors and the old IOC restriction must fail.
Compiled controls require exact expected exit codes and causal diagnostics.
Signals, sanitizer diagnostics and timeouts cannot count as expected failures.
The timeout controller kills and waits for its own process group.
Host models and sanitizers do not execute TFU or qualify physical DMA behavior.
