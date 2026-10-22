# BCM2712 bounded bin/render queue contract

`BCM2712_V3D_QUEUE_PROBE` in `bcm2712_v3d_queue.c` and `EMBERV3DQUEUE`
implements one bounded tile clear/store after successful takeover and DMA.
All register, sequence and packet facts below are pinned to
Raspberry Pi Linux `43c132e8863c3bff3647033b6a7d2bf87b15501c`
(`v3d_sched.c`, `v3d_gem.c`, `v3d_regs.h`) and Mesa 26.2.4
(`src/broadcom/cle/v3d_packet.xml`, `src/gallium/drivers/v3d/v3dx_{draw,job,rcl}.c`,
`src/broadcom/common/v3d_util.c`, release SHA256
`bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9`).

## Scope and prerequisites

A separate opt-in `BCM2712_V3D_QUEUE_PROBE` and `EMBERV3DQUEUE`
configuration, after a completed takeover, in the same boot as the accepted
DMA experiment. The probe performs its own bounded allocations and republishes
the MMU exactly like the DMA experiment, keeping its bounded MMUC/TLB waits
and control read-back (same six rules: one segment, 4 KiB alignment, 32-bit
window, no overlap, full-value control writes, retention after the first
publishing write). The CORE interrupt status must read zero before the
binner starts, so a stale latch cannot fake completion. No IRQ handler yet: completion is observed
by polling latched interrupt status with delivery still masked. No DRM, no
user command submission, no draws, no shaders, no GMP.

One clear-and-store job: the binner builds one empty tile list and the
render stage clears a single 64x64 tile to a deterministic color and stores
it to a raster buffer that the CPU verifies word by word.

## Submission facts (pinned)

All offsets are CORE block unless noted. Cache invalidation for ver >= 41 is
two writes; L3/L2C steps are version-gated off in the pinned source.

| Step | Register | Value |
| --- | --- | --- |
| Bin pre | `PTB_BPOS` 0x30c | 0 |
| Invalidate | `CTL_L2TCACTL` 0x30 | `L2TFLS` bit0 \| FLM_FLUSH(0) << 1 |
| Invalidate | `CTL_SLCACTL` 0x24 | 0x0f0f0f0f (0xf in TVCCS/TDCCS/UCC/ICC) |
| Bin memory | `CLE_CT0QMA` 0x170 | tile_alloc VA |
| Bin memory | `CLE_CT0QMS` 0x174 | tile_alloc size |
| Bin memory | `CLE_CT0QTS` 0x15c | ENABLE(bit1) \| tile_state VA |
| Bin submit | `CLE_CT0QBA` 0x160 | BCL VA (start) |
| Bin submit | `CLE_CT0QEA` 0x168 | BCL end — **this write starts the binner** |
| Bin done | `CTL_INT_STS` 0x50 | FLDONE bit1 latched; clear through `CTL_INT_CLR` 0x58 |
| Invalidate | same two writes | before the render submit |
| Render submit | `CLE_CT1QBA` 0x164 | RCL VA (start) |
| Render submit | `CLE_CT1QEA` 0x16c | RCL end — **this write starts the render** |
| Render done | `CTL_INT_STS` 0x50 | FRDONE bit0 |

Interrupt bits (CORE): FRDONE bit0, FLDONE bit1, OUTOMEM bit2, SPILLUSE
bit3, TRFB bit4, GMPV bit5. OUTOMEM/SPILLUSE/TRFB are failures for this
experiment, not completion. MMU faults (HUB `MMU_CTL` 0x1200 bits 27/20/12)
always win over a latched done bit. Each done wait is bounded to 500 ms;
each cache wait to 100 ms if ever polled. Statuses must latch with masks
still in force; only `INT_CLR` writes are permitted on the interrupt block.

## Memory

Seven objects, same bus_dma rules as the DMA experiment:

| Object | Size | Rationale |
| --- | --- | --- |
| Page table | 4 MiB | 32-bit VA subset |
| Illegal scratch | 4 KiB | MMU illegal-address target |
| tile_alloc | 12 288 B | `v3d_tile_alloc_sizes(1,1,1,0,4096)`: align(128,4096)+8192, zero draws |
| tile_state | 4 KiB | 256 B needed per tile |
| BCL | 4 KiB | the packet stream below |
| RCL | 8 KiB | packet stream plus the inline generic tile list |
| Output image | 20 KiB | 64x64 RGBA8 raster (16 KiB) plus one canary page |

PTE mapping: this job maps each buffer at its own DMA address through
the MMU (identity translation; alias-address translation was proven by
the DMA experiment). BCL/RCL read-only, tile_alloc/tile_state/output
writable. Both control lists are snapshotted before publication and
compared after completion; a GPU write into them fails the experiment. tile_alloc and
tile_state legitimately change when the binner runs; the experiment must
not treat their contents as canaries. BCL/RCL/output/scratch are canaries.

## BCL packets (V3D 7.1, from v3dx_draw.c and v3dx_job.c)

Packet fields are defined in `v3d_packet.xml` with bit positions starting
after the 8-bit opcode header; encode exactly as `gen_pack_header.py` does.

1. `TILE_BINNING_MODE_CFG` (120): width-1 = 63 at [47:32], height-1 = 63 at
   [63:48], log2 tile width = 3 at [10:8], log2 tile height = 3 at [13:11],
   tile allocation initial block size 128b = 1 at [3:2], tile allocation
   (overflow) block size 128b = 1 at [5:4].
2. `FLUSH_VCD_CACHE` (19).
3. `OCCLUSION_QUERY_COUNTER` (92) with enable = false.
4. `START_TILE_BINNING` (6) — required after any prefix state.
5. `FLUSH` (4) — the epilogue; the hardware caps the bin CLs with a return.

## RCL packets (from v3dx_rcl.c)

1. `TILE_RENDERING_MODE_CFG_COMMON` (121 sub-id 0): image width 64 at
   [23:8], image height 64 at [39:24], number of render targets - 1 = 0 at
   [7:4], log2 tile width/height = 3 at [54:52]/[57:55], early-Z disable
   bit [46], depth-buffer disable bit [44].
2. `TILE_RENDERING_MODE_CFG_RENDER_TARGET_PART1` (121 sub-id 2): clear color
   low 32 bits at [63:32], internal type and clamping = RGBA8 (8) at
   [31:27], internal BPP = 32 (0) at [26:25], stride - 1 = 31 (64 texels
   32bpp is 32 units of 128 bits per two rows) at [24:18], base address 0
   at [17:7].
3. `TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES` (121 sub-id 1).
4. `TILE_LIST_INITIAL_BLOCK_SIZE` (126): auto-chained, first block 128b —
   must match the binning config.
5. `MULTICORE_RENDERING_TILE_LIST_SET_BASE` (123): address = tile_alloc VA
   at [31:6], tile list set number 0.
6. `MULTICORE_RENDERING_SUPERTILE_CFG` (122): number of bin tile lists - 1
   = 0 at [63:61], total frame 1x1 tiles at [43:32]/[55:44], 1x1 supertiles
   at [23:16]/[31:24], supertile size in tiles - 1 = 0 at [7:0]/[15:8].
7. Initial double clear (GFXH-1742 style), each pass led by
   `TILE_COORDINATES` (124: 0,0) as Mesa does: `END_OF_LOADS` (26),
   `STORE_TILE_BUFFER_GENERAL` (29) with buffer = NONE (value 8),
   `CLEAR_RENDER_TARGETS` (25), `END_OF_TILE_MARKER` (27); twice; then
   `FLUSH_VCD_CACHE` (19).
8. Generic tile list inline in the RCL buffer:
   `TILE_COORDINATES_IMPLICIT` (125), `END_OF_LOADS`, `PRIM_LIST_FORMAT`
   (56) with list triangles (2), `SET_INSTANCEID` (54) with zero (the PTB
   assumes zero and the hardware will not set it), `BRANCH_TO_IMPLICIT_TILE_LIST` (21),
   `STORE_TILE_BUFFER_GENERAL` with buffer = render target 0, output image
   format rgba8 = 27, memory format Raster = 0, dither None = 0, decimate
   sample 0, height 64, raster byte stride 256 ("Height in UB or Stride"),
   address = output image VA at [95:64], `END_OF_TILE_MARKER`,
   `RETURN_FROM_SUB_LIST` (18); then `START_ADDRESS_OF_GENERIC_TILE_LIST`
   (20) covering exactly that inline range.
9. `SUPERTILE_COORDINATES` (23) with column 0, row 0; the packet has a
   two-byte payload with two 8-bit fields (unlike `TILE_COORDINATES` 124).
10. `END_OF_RENDERING` (13).

## Verification

The clear color is a single deterministic word with four distinct bytes,
for example 0x305e7b4c (RGBA channels low to high, matching the rgba8
output format). Translation PASS requires: FRDONE latched with no MMU
fault and no OUTOMEM/SPILLUSE/TRFB, all 4096 output words equal to the
clear color, the output canary page untouched, BCL/RCL contents unchanged
after POSTREAD|POSTWRITE, and the scratch still zero. Partially cleared
output, wrong color order, cleared canary or a fault are distinct failures
with their own verdict strings. tile_alloc/tile_state changes are expected
evidence, not failures. After the first publishing MMU write everything
GPU-exposed is retained until reboot, on success and on every failure;
the FLDONE path must clear its status bit before the render submit so
FRDONE is unambiguous.

## Contracts and review

Same actual-source host contract family as the DMA experiment: fake
takeover window, fake bus_dma with CPU/GPU views, and a fake GPU whose
control-list executor recognizes exactly the packet stream above (byte
pattern), simulates the clear-and-store through the fake MMU and latches
FLDONE/FRDONE. Cases: every allocation/load/sync/MMIO failure, both done
timeouts, fault-before-done priority, OUTOMEM/SPILLUSE as failures,
wrong-color and partial-clear mismatches, canary writes, INT_CLR misuse,
BCL/RCL written by the GPU, retention after publication, one-shot.
Mutants: at least missing second invalidate, missing INT_CLR after FLDONE,
wrong store address/format/stride, missing START_TILE_BINNING, missing
FLUSH epilogue, release-after-publication. Independent review and a
complete clean-commit build of `EMBERV3DQUEUE` before any installation;
the ordinary CM5 recovery bundle must still verify.

## Hardware applicability


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/bcm2712-cm5/graphics.md).
A bounded tile clear/store does not establish general command-list submission or safe arbitrary-job recovery.

## Bounds and related contracts

This probe exercises the bin/render engines on one tile without shaders.
It does not qualify general geometry, OOM recovery, GMP or queueing.
Separate contracts cover [interrupts](bcm2712-v3d-irq-plan.md),
[fault recovery](bcm2712-v3d-fault-plan.md), and the
[native render interface](../../../../external/bsd/drm2/bcmv3d/docs/bcm2712-v3d-render.md).
Each stage stays opt-in with
contracts and review first.
