# Native V3D TFU admission

The native V3D 7.1 driver accepts a bounded Mesa 26.2.4 TFU subset. This is a
software admission policy. It is not a complete hardware register description
or full TFU capability. `GET_PARAM(SUPPORTS_TFU)` remains zero.

## File ownership and roles

`SUBMIT_TFU` uses the existing V3D UAPI. Output handle slot 0 is required.
At most four inline handles form a nonzero prefix. A nonzero entry after a
zero is invalid. Each handle must select a native BO owned by the submitting
DRM file. Private page-table and sink storage is never an image BO.

The driver holds one reference per unique GEM object. Repeated handles and
different handles for the same object share that reference. Distinct objects
with the same reservation remain invalid under the common reservation contract.
Reservation sorting cannot exchange source and destination roles.

IOA must lie inside the output BO. IIA must lie inside one of the admitted BOs.
Interior layer and mip offsets are allowed. Initial ranges use exclusive ends
and 64-bit arithmetic. No adjacent or unreferenced BO becomes accessible.

The GPU MMU maps each complete admitted BO across its DMA segments. The output
is read/write. All other objects are read-only. An input/output alias receives
read/write permission once. This bounds storage access. It does not validate
an entire tiled image, mip footprint, overlap or in-object padding.

## Explicit mode subset

Input and output layouts are raster (0), LINEARTILE (3), UBLINEAR1 (4),
UBLINEAR2 (5), UIF_NO_XOR (6) and UIF_XOR (7). Types 0 through 19 support copy
and filtered mip requests. Types 20 and 29 through 31 support copy only.
R4 (25) is excluded because the pinned Mesa format table does not emit it.

Width and height must be 1 through 16384. This is an explicit Mesa policy,
not a claim about a hardware field maximum. Raster strides must be at least
the width. UIF strides must be nonzero. Other tiled layouts use zero stride.
Mip count is 0 through 14. DIMTW is set exactly when mip count is nonzero.
Mip output must use a tiled layout.

ICA, IUA, all coefficients, flags, extensions and reserved padding must be zero.
Unknown configuration bits are refused. User ICFG IOC may be zero or one.
The kernel always adds IOC when it writes ICFG. IOC requests the completion
interrupt. The ICFG write starts execution.

SAND, planar YUV, coefficient conversion, multisync and other hardware modes
remain unsupported. The driver does not invent a linear mip envelope. Mesa
can place generated levels before the largest level's base address.

## DMA and retirement

Every unique input has one PREWRITE and POSTWRITE pair. The destination has
combined PREWRITE/PREREAD and POSTWRITE/POSTREAD, including for an alias. This
preserves CPU dirty bytes outside partial writes. Each job retains its existing
fence, IRQ-count, conversion-count, idle-state and clean-MMU checks.

Only verified retirement, PTE revocation and a completed MMU flush permit POST
synchronization and reference release. An uncertain execution retains all unique
references, busy states and the persistent four-entry vector until reboot.
A later TFU, CL or CSD submission cannot replace that retained ownership.

GPU PTEs use 4096-byte units independently of kernel `PAGE_SIZE`. The walker
maps each 4 KiB part of every translated DMA segment. This source contract does
not establish physical support for a kernel with 64 KiB pages.

## Reproduce the source contracts

Run these host commands from a checkout. `CC`, `CFLAGS` and `TEST_RUNNER` select
a cross compiler and a compatible execution runner where supported.

```sh
sh ember/tools/bcm2712-v3d-job-contract.sh
ruby ember/tools/bcmv3d-ptes-contract.rb
ruby ember/tools/bcmv3d-tfu-reject-contract.rb
```

`V3D_JOB_EVIDENCE`, `PTES_EVIDENCE` and `TFU_REJECT_EVIDENCE` retain their inputs,
commands and logs in a new directory. The contracts extract actual native
functions. Controlled DRM, DMA, IRQ and execution services check reference,
reservation, mapping and failure order. They do not execute a GPU or verify
filtered pixels. The PTE contract checks 4 KiB and 64 KiB host page models,
selected pages, permissions, complete SG coverage, revocation and untouched
neighbor/guard entries. Compiled causal controls require their exact CHECK,
exit 1 and no sanitizer diagnostic. Signal, recovering-sanitizer and inherited
pipe controls test the runner gate.

Use the [finite raster checker](bcm2712-v3d-tfu-check.md) for physical copy
acceptance. Host mode acceptance does not establish tiled or mip texel
correctness, recovery from a GPU fault, or a working Plasma session.

## Provenance

The policy follows Mesa 26.2.4 `src/gallium/drivers/v3d/v3dx_tfu.c`,
`v3d_formats.c`, and `src/broadcom/common/v3d_tfu.h`. The selected prepared-source
archive has SHA256 `e69dbb44f9626a3e05c74922aba127e586df8bad8cddf48be93b7122d44789c1`.
The bounded handle model and kernel IOC rule were checked against Linux V3D
[`v3d_submit.c`](https://github.com/torvalds/linux/blob/a5ebb76233b79db01e82e061173cb73c6d2b5c6b/drivers/gpu/drm/v3d/v3d_submit.c),
[`v3d_sched.c`](https://github.com/torvalds/linux/blob/a5ebb76233b79db01e82e061173cb73c6d2b5c6b/drivers/gpu/drm/v3d/v3d_sched.c)
and `v3d_regs.h` at the same revision. The native execution and retention
mechanism remains specific to EmberBSD.
