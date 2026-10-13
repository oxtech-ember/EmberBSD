# A733 PowerVR firmware-derived queries

The native PowerVR driver implements `RUNTIME_INFO_GET`, `QUIRKS_GET`, and
`ENHANCEMENTS_GET`. These queries enable the next Mesa device-enumeration step.
They do not implement execution contexts, free lists, submission, or rendering.

## Provenance and ownership

The profile uses parsed metadata from the firmware image prepared in stage 13.
The A733 adapter copies only required fields into `pvr_query_input`.
The DRM driver derives and retains an immutable `pvr_query_profile`.
It does not keep firmware pointers or read GPU registers during an ioctl.
Registration requires BVNC `36.56.104.183` and firmware `1.1.6976702`.
A different firmware version requires explicit validation and profile support.

The accepted firmware source and SHA256 are recorded in
[the metadata contract](a733-gpu-fwinfo.md). Its SHA256 is
`1db1c399c17401d1f79d46c880db81c724d748c784d4639433b076aba2f9c0d2`.
The contract runner checks that hash before it parses the input file.

The MIT adaptation uses Linux revision
`7b63ef2d55f24519e7e9e5f4d15dbea03f126e40`:

- [`pvr_drv.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_drv.c) defines runtime formulas and query negotiation.
- [`pvr_drv.h`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_drv.h) defines mandatory object fields.
- [`pvr_free_list.c`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_free_list.c) defines minimum PM page counts.
- [`pvr_rogue_defs.h`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_defs.h) defines register limits.
- [`pvr_rogue_fwif_client.h`](https://github.com/torvalds/linux/blob/7b63ef2d55f24519e7e9e5f4d15dbea03f126e40/drivers/gpu/drm/imagination/pvr_rogue_fwif_client.h) defines the PM page and free-list size limit.

## Verified input and derived output

The firmware reports a 19,456-dword common store, 12 partitions, 16×16 tiles,
and two minimum output registers per pixel. It reports one cluster and six
tiles in flight. `ROGUEXE` and `COMPUTE_OVERLAP` are present.
BRN mask `0x201` contains 44079 and 63553. ERN mask `0xf5` contains 35421,
38748, 42290, 42606, 47025, and 57596.

| Runtime field | Derived value | Unit |
| --- | ---: | --- |
| `free_list_min_pages` | 25 | 4096-byte PM pages |
| `free_list_max_pages` | 524288 | 4096-byte PM pages |
| `common_store_alloc_region_size` | 11264 | dwords |
| `common_store_partition_space_size` | 6144 | dwords |
| `max_coeffs` | 4096 | registers |
| `cdm_max_local_mem_size_regs` | 4096 | registers |

The partition formula is `16 * 16 * 12 * 2`.
BRN44079 caps the allocation region at `768 * 4 * 4 - 1024`.
ERN38748 removes the pending shared-register reservation.
The coefficients are `11264 - 6 * 1024 - 1024`.
The 2 GiB free-list maximum is a protocol limit, not an allocation guarantee.
No free-list allocation ioctl is implemented by this query change.

Only the upstream UAPI compatibility lists are exported.
Mandatory quirk candidates are 47217, 49927, and 62269.
Optional candidates are 48545 and 51764. The current intersection is empty.
Enhancement candidates are 35421 and 42064. The current result is `[35421]`.
These lists are not copies of every firmware or Mesa-internal workaround.

## Query contract

A null outer pointer returns the required size without accessing user memory.
Runtime objects require all 32 bytes. Quirk and enhancement objects require
all 16 bytes, including the final padding field specified by pinned Linux.
Short objects return `EINVAL`. Runtime queries only write output.
List queries reject nonzero padding and unknown nonzero extension bytes.
Unknown nonzero extension bytes return `E2BIG`.

A null nested list pointer requests its count. Initialize the query to zero.
Pinned Linux updates `musthave_count` only on the quirk data-copy path.
A nonnull list with insufficient capacity returns `E2BIG`; it is not truncated.
The driver copies only the reported number of 32-bit identifiers.
Output extension bytes are cleared. Invalid address spans return `EFAULT`.
A failed copy can leave a partial output; discard it after any error.
The outer argument size changes only after successful output copying.

## Source and native checks

Run the actual parser, adapter, and query contract with the pinned firmware:

```sh
PVR_QUERY_FIRMWARE=/path/to/rogue_36.56.104.183_v1.fw \
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/pvr-query-contract.sh
```

The tests use real firmware metadata and change individual input facts.
They check arithmetic bounds, missing features, version rejection, two-phase
queries, short objects, array capacity, padding, extension bytes, and copy faults.
The [render-node acceptance utility](a733-pvr-render.md#source-checks-and-board-acceptance)
checks the running kernel against these derived values.
On 2026-10-13, the utility passed all 115 identity/runtime checks on Zero 3W
with kernel `0e89a6ffb4d726c64cf402049758f960d29a9655`.
It checked the values above through the native render node.
Registration before firmware preparation correctly returned `EBUSY`.
This result establishes the query ABI on the tested board, not Vulkan execution.

Mesa 26.2.4 still requires a real global free list during device creation.
Each queue creates transfer, compute, query-compute, and render contexts.
A compute-only application follows those same creation paths.
The upstream BXM experimental opt-in remains required.
