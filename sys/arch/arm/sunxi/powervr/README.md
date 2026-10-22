# Native PowerVR firmware ABI headers

These headers define the shared host/firmware records used by the Rogue GPU.
They preserve upstream structure, enumeration, field, and constant names.
They do not implement DRM, DMA, device access, firmware boot, or job submission.
The current host interface requires 64-bit pointers and little-endian byte order.

## Source and license

The source is the Linux PowerVR driver at revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`.
The original headers offer `GPL-2.0-only OR MIT`.
This import selects MIT and preserves Imagination Technologies' attribution.
The complete notice is in [LICENSE](LICENSE).
The adaptation and tests were developed with AI assistance.

Source URL prefix:
`https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/`

Fetched on 2026-10-12. Hashes below identify the unmodified source files.

| Upstream file | SHA256 |
| --- | --- |
| [pvr_rogue_fwif.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif.h) | `ac934aafc4a129e9af197ebb1a9b9d49d3214ad12951a5aaa0140f9f68608457` |
| [pvr_rogue_fwif_common.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_common.h) | `979ae32fdc3c96e4b0ae38e3b288b4230a66d87b7aa07fb7d5f4d5f6fb8cdcce` |
| [pvr_rogue_fwif_shared.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_shared.h) | `c84297ec46c7cbf3f15abf0407cfa78a654af32d902dd79495ae820244c0ad2e` |
| [pvr_rogue_fwif_check.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_check.h) | `92745993eb274decb9e4e6806ee912836d16ec8619dee3df23a0b7ad02f5d977` |
| [pvr_rogue_fwif_shared_check.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_shared_check.h) | `ef0ba4978214c9e42600f831dd341ffb6be4cf8a1f092d68e08900e2ea1fa628` |
| [pvr_check.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_check.h) | `8ec73428fea35ea5b71ba020b0a816d07c9e5ef7108e6e0cdd5ae60b6fc6392f` |
| [pvr_rogue_defs.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_defs.h) | `7c4c6902f7e456694bd3d64608eae48cdfccc223750773f07648de712e2d4ced` |
| [pvr_rogue_cr_defs.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_cr_defs.h) | `7421bd9524a485e729421fa1452e09d6687d7a34b092c2502a3c1271b0295b7c` |
| [pvr_rogue_fwif_client.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_client.h) | `3abfc8228e208f9d3dfa79b40f783e94ef5d68887e61270f5e6a2d698b8f4a71` |
| [pvr_rogue_fwif_client_check.h](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_client_check.h) | `5e2c1fff1470167d3bb40c0df0f01ca877a07e16d4cc6da10153e87c1b8bb411` |

## Scope of the adaptation

- `pvr_rogue_fwif.h`, `pvr_rogue_fwif_common.h`,
  `pvr_rogue_fwif_shared.h`, and `pvr_rogue_fwif_client.h` retain the complete
  dependent ABI declarations.
  This keeps the real command unions and all upstream layout checks together.
- The original two `*_check.h` files retain all 476 upstream assertion calls.
  The client check header retains all 108 additional upstream checks.
  Four further assertions remain in `pvr_rogue_fwif.h`.
- `pvr_fw_abi.h` supplies native fixed-width C types, alignment, bit, and
  alignment-rounding primitives. It checks host assumptions at compile time.
- Linux `u8`, `u16`, `u32`, `u64`, and `s32` become the corresponding native
  fixed-width types. `aligned_u64` becomes `pvr_fw_aligned_u64`.
- `__aligned`, `BIT`, `BIT_MASK`, `ALIGN`, and `static_assert` become
  native equivalents.
  The client header replaces `SZ_4K` and `SZ_2G` with their exact unsigned values.
  Every retained `BIT_MASK` argument is below 32, so `PVR_FW_BIT` is equivalent.
  No structure fields, array lengths, enum values, or wire constants change.
- `pvr_fw_abi_defs.h` extracts only `ROGUE_FW_MAX_NUM_OS` from `pvr_rogue_defs.h`
  and `ROGUE_CR_TIMER` from `pvr_rogue_cr_defs.h`.
  These are the only definitions those dependencies supply to this subset.
- `pvr_check.h` uses native compile-time assertions.
  Its array check expresses the upstream count-one check through array type
  compatibility. It rejects pointers. Like upstream, it also accepts a
  fixed-size array compatible with the corresponding incomplete array type.

No Linux kernel headers or compatibility framework are required.
The retained upstream `rogue_fwif_compchecks_bvnc_init()` is a plain C helper.
The import adds no initialization sequence and invokes no helper.
The MIPS page-table and boot-data encoder is separate from this shared ABI.

See [the contract and validation limits](../docs/a733-gpu-fwabi.md).
