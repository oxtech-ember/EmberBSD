# A733 CPU compute-context serializer

The helper prepares one fresh compute context and one client-queue submission.
It serializes CPU bytes only. It does not execute GPU instructions.
It does not allocate memory, map firmware addresses, synchronize DMA, or access registers.
It does not insert a command into the live KCCB.

The implementation is
[`sun60i_a733_gpu_fwcompute.c`](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.c).
The API is in
[`sun60i_a733_gpu_fwcompute.h`](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute.h).
It requires a 64-bit little-endian host.
The kernel source list and portable contract runner include this helper.
Diagnostic stage 18 loads a packed compiler image and calls this helper on target;
see the stage section below.

## Stage 18: packed compiler image on target

Stage 18 (`hw.sun60igpuid0.stage=18`) is the first serializer consumer.
It runs only on the CPU after a fully successful stage 17.
The file format lives in
[`sun60i_a733_gpu_fwcompute_file.h`](../../sys/arch/arm/sunxi/sun60i_a733_gpu_fwcompute_file.h):
one 104-byte little-endian header with magic `0x314d4337`, version 1,
the exact BVNC, four-byte-aligned payload spans for the USC, PDS, and CDM-stream
bytes and the 56-byte static state, their GPU addresses, the context-state
address, priority, and identifiers. The parser validates every bound,
alignment, and overlap and keeps the total file at most 256 KiB.
The production parser, span derivation, packer, and digest cross-check run in
[`a733-gpu-fwfile-contract.sh`](../tools/a733-gpu-fwfile-contract.sh).

The typed 96-byte command is kernel-owned, as in the upstream submission:
the stage fills only the CDM control-stream base and the context-state base
address from the file header and leaves every other field zero.
The six firmware object spans are declared in the fixed window
`0xc0800000..0xc0808138`, which lies between the firmware code heap end
(`0xc0000000` plus the parsed code allocation, 102400 bytes in the accepted
image) and the fixed config pages at `0xc0fd0000`.
The stage verifies at runtime that this window stays clear of the live
code and work mappings.
Nothing in that window is allocated by the firmware heap, backed, or
page-table mapped: the serialized image is a CPU artifact only.

The stage digests the complete 33 KiB serialized image with the in-kernel
SHA-256 and reports it through `hw.sun60igpuid0.compute_state`.
The expected digest comes from the packer
[`a733-gpu-compute-firmware`](../tools/a733-gpu-compute-firmware.c),
which packs the same artifacts, runs the same serializer with the same
spans and command fields, and prints both the file and image digests.
The packed file installs as `a733-compute.bin` in the `powervr` firmware
directory. No KCCB publication, DMA mapping, or GPU execution happens.

## Input and ownership contract

Allocate `sun60i_a733_gpu_fwcompute_image` from caller-owned heap memory.
The image is approximately 33 KiB. Do not put it on the kernel stack.
Pass its exact size to `sun60i_a733_gpu_fwcompute_prepare()`.
The caller must serialize access to all input and output spans.
The helper does not retain pointers after the call.

Supply six independent firmware spans with these exact object sizes.
Every firmware address must have 8-byte alignment.
The caller must first validate those spans against the actual firmware heap and mappings.
Numerical non-overlap does not prove backing-memory independence.

| Object | Bytes | Role |
| --- | ---: | --- |
| `rogue_fwif_fwcomputecontext` | 232 | Common CDM context and actual static state |
| `rogue_fwif_cccb_ctl` | 32 | Initially idle client-queue control |
| Client command ring | 32768 | Fresh compute queue |
| `rogue_fwif_compute_ctx_state` | 4 | Initially zero context-switch flags |
| Timeline word | 4 | Initially zero completion value |
| External `rogue_fwif_fwmemcontext` | 32 | Existing VM context; the helper does not modify it |

Supply actual USC, PDS, and CDM-stream spans with CPU pointers, GPU addresses, and lengths.
All three CPU spans must be distinct from each other, the input record, and the output image.
Their GPU spans must be distinct and fit in the 40-bit GPU address range.
Lengths must be nonzero multiples of four and no greater than `UINT32_MAX`.
USC and PDS GPU addresses require 16-byte alignment. The CDM stream requires four-byte alignment.
The typed command's stream address must equal the supplied CDM-stream address.
The context-state GPU address must be nonzero, 16-byte aligned, and representable in 40 bits.

Supply the real 56-byte `rogue_fwif_static_computecontext_state` and typed 96-byte compute command.
An entirely zero static state is rejected. Individual zero fields can be valid.
The helper does not decode PDS relocations or validate USC instructions.
It does not copy or inspect the program bytes. Nonempty spans are a required input contract, not compiler attestation.
Register packing, resource backing, and accessible GPU mappings remain caller responsibilities.

`EINVAL` rejects invalid lengths, arithmetic overflow, alignment, overlap, priority, or command flags.
Priorities are the upstream low/medium/high values 0 through 2.
Only `PREVENT_ALL_OVERLAP` and `SINGLE_CORE` command flags are permitted.
The frame number is zero, as in the upstream compute job initializer.
Every error preserves all output bytes.
The helper assigns named fields after validation. It does not copy input padding or reserved words.

## Initial direct-CDM scope

The initial scope targets BVNC `36.56.104.183` with the pinned firmware profile.
The [firmware parser](a733-gpu-fwinfo.md) reports feature masks
`0x92d9ff57fdbf7cdb` and `0x1f`, and BRN mask `0x201`.
CDM control-stream format is 1. Cluster grouping and compute Morton support are absent.
`TPU_DM_GLOBAL_REGISTERS` and `GPU_MULTICORE_SUPPORT` are present.
Linux derives `CDM_USER_MODE_QUEUE` only for stream formats 2 through 4.
BRN 49927 is absent in this profile.

Nonzero user-queue registers, stream offsets, Morton state, cluster state, BRN 49927 state,
`num_temp_regions`, or `execute_count` return `EOPNOTSUPP`.
These fields are not silently cleared to accept a packet.
The pinned Linux compute stream has no `num_temp_regions` entry.
Mesa's initial single-core submission emits `execute_count = 0` even when the multicore feature is present.
A future caller must verify that runtime case. This helper does not claim general multicore submission support.
A genuine compiler packet may require a bounded extension before hardware execution.

## Exact wire result

The common context uses CDM 4, the supplied priority and identifiers, and deadline 30000 ms.
Its address fields refer to the supplied CCCB, control, context-state, and external VM objects.
The static compute state occupies bytes 152 through 207 of the 232-byte compute context.
Remaining initial context fields are zero, as in upstream zero-initialized allocation.

| Ring offset | Record | Bytes |
| ---: | --- | ---: |
| 0 | CDM header, opcode `0x2abc80cd` | 40 |
| 40 | `rogue_fwif_cmd_compute` | 96 |
| 136 | UPDATE header, opcode `0x2abc00d5` | 40 |
| 176 | Timeline UFO address and update value 1 | 8 |

The complete sequence uses **184 bytes**. It does not use 192 bytes.
The original producer aligns command payloads to eight bytes.
Its actual behavior takes precedence over the shared-header comment about 16-byte write alignment.
The unused ring bytes stay zero.
The shared control's write, read, and dependency offsets stay zero; its wrap mask is 32767.

The separate 88-byte KCCB template uses KICK opcode `0x2abc0065`.
It carries the common-context address, write offset 184, and wrap mask 32767.
It contains no cleanup objects or work-estimation data.
The helper never publishes this template.
A later consumer must establish mappings, DMA visibility, ownership, and queue ordering.
KCCB acknowledgment alone is not compute completion. The timeline update is a separate firmware operation.

## Source and license

The adaptation selects MIT from Linux PowerVR revision
[`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`](https://github.com/torvalds/linux/tree/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination).
It preserves Imagination Technologies' authorship. The helper contains the full MIT notice.
The new client ABI headers retain all 108 upstream client layout assertions.
The existing ABI import retains its original checks.
Additional native checks cover the 232-byte compute context and its offsets.
These additional checks are also compiled against the original upstream structure in the oracle.
See the [ABI provenance and license](../../sys/arch/arm/sunxi/powervr/README.md).
The native implementation and tests were developed with AI assistance.

The source functions are `init_fw_context()` and `reg_state_init()` in
[`pvr_queue.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_queue.c),
and the control, command-write, and KICK producers in
[`pvr_cccb.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_cccb.c).
Their unchanged SHA256 values are checked by the contract runner.
The optional-field gates come from
[`pvr_stream_defs.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_stream_defs.c)
and [`pvr_device.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_device.c).
Mesa 26.2.4 provides the BXM profile in `src/imagination/common/device_info/bxm-4-64.h`.
Its `src/imagination/vulkan/pvr_arch_job_compute.c` defines the initial `execute_count` behavior.

## Reproduce the software contract

Run the normal CPU contract from the repository root:

```sh
sh ember/tools/a733-gpu-fwcompute-contract.sh
```

For the independent oracle, obtain the listed original files from the pinned Linux revision.
Set `FWCOMPUTE_UPSTREAM_DIR` to that directory. The runner verifies 13 file hashes.
It compiles the original ABI headers in a separate translation unit.
It executes nine unchanged producer functions with memory-only transport adapters.
It compares the complete context, control, state, ring, and KICK bytes.
Expected bytes do not depend on native output.

```sh
FWCOMPUTE_UPSTREAM_DIR=/path/to/pinned/linux-pvr \
FWCOMPUTE_TEST_CFLAGS='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' \
sh ember/tools/a733-gpu-fwcompute-contract.sh
```

The runner reports when the optional oracle is disabled.
`CC` selects the host compiler. `TMPDIR` selects temporary storage.
`FWCOMPUTE_KEEP_WORK=1` retains the generated oracle and executable for audit.

The checked run passed 986541 ASan/UBSan checks with the original producer oracle.
Ten causal mutations failed, including early writes, padding leaks, dropped payload fields,
unknown flags, aliases, wrong completion values, and a 192-byte KICK offset.
Target GCC16 compilation, default GCC path analysis, Clang Static Analyzer, and Cppcheck passed.
Host analysis covered the native helper, contract, and extracted original producers.
The original producers retain nine const-style findings and one GNU void-pointer arithmetic finding in Cppcheck.
They are recorded separately and are not suppressed. Native helper and contract analysis are clean.
Valgrind was not used on the macOS ARM64 host. ASan/UBSan is the stated substitute.

The test fixture contains synthetic data. It is not an executable GPU program.
Before hardware use, require a manifest from the real Mesa compiler and generators.
That manifest must bind the source revision, target BVNC, shader input, binary hashes,
USC/PDS/CDM lengths, relocation addresses, resource map, and actual static compute state.
No compute dispatch, firmware submission, output-buffer result, or rendering is proved here.

## Physical stage-18 result

On 2026-10-12, stage 18 passed on a 4 GiB Orange Pi Zero 3W.
The GCC 16.2 `EMBER64 #0` bundle used source
`b09cd6a4c3899234da87b3c711776075643e4408` (built from the identical
pre-rebase tree `6b5248109ecf`) and includes the accepted
fan-control commits. All stages 1 through 18 returned zero with PLL_GPU0
selected, on the second run after renaming the packed file into place
(the first run correctly returned `ENOENT`).
The MMUCACHE gate observed slot 1, return word 1, read/write offsets 2/2,
flags `0x0400001f`, sync object `0xc0104400`, and both BVNC readings
`0x00240038006800b7`.

The packed image came from the r2 bounded Mesa generator with the corrected
IDFWDF sampler window: USC `0xe000000000+1744`, PDS `0xda0000a000+720`,
CDM stream `0x8000004000+116`, context state `0xda00000000`, 56-byte static
state. The kernel serialized it to the exact 184-byte client ring and the
KICK template, and its SHA-256 over the complete 33 KiB image matched the
host packer digest byte for byte:
`9e611c516dafb8886088ee4690c0d92f153a76a7b624a0a215c074a446161e3a`.

The run stayed CPU-only: no firmware-heap allocation, page-table entry,
DMA, KCCB publication, or GPU execution. The board kept running normally
after the stage. The declared span window stays unmapped; a future mapping
and submission stage must allocate from a real firmware heap before any
hardware compute.

```sh
sysctl -w hw.sun60igpuid0.clock_source=1
sysctl -w hw.sun60igpuid0.stage=18   # after stages 1..17
sysctl hw.sun60igpuid0.result hw.sun60igpuid0.compute_state
```

## Version-2 packed file and profile guard

The packed file now carries the complete nine-resource r2 manifest:
header magic `0x324d4337`, version 2, the BVNC, priority and identifiers,
the context-state address, a 32-byte payload digest, and one 24-byte
entry per resource (virtual address, payload span, role, read-only and
SLC-bypass flags). The payload holds the whole general, PDS and USC
artifacts plus the 56-byte static state. Version 1 carried only three
program spans; the accepted 2026-10-12 result above used it, and version
2 supersedes it. The parser requires each of the nine roles exactly once,
four-kilobyte-aligned page-disjoint addresses, and nonoverlapping spans.
The kernel stage verifies the payload digest with the in-kernel SHA-256
and rejects the file with `EAUTH` on any mismatch. It also pins the
firmware profile: BVNC equality, feature masks `0x92d9ff57fdbf7cdb` and
`0x1f`, and BRN mask `0x201` from the parsed firmware metadata must hold
before any compiler byte is used.

The packer emits the version-2 file and additionally runs the portable
compute VM encoder over the resource table (three windows, three
directories, seventeen leaves) before printing both digests.
