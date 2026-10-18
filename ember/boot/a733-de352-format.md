# A733 DE3.52 RGB and RCQ data format

`sun60i_a733_de352.c` encodes one linear progressive XRGB8888 layer and
RCQ descriptors. It performs no I/O. It does not allocate DMA memory, map a
display aperture, submit a list, or establish scanout readiness. The
[native DMA preparer](a733-de352-dma.md) uses these encoders. Native DE
submission and output integration remain separate steps.

## One layer

The RGB input has width, height, byte pitch, a device-visible DMA address,
and the number of mapped bytes. The format is fixed to XRGB8888: B, G, R,
then unused X bytes in little-endian pixel memory. Dimensions must be 1
through 8192. Pitch must cover the visible row and be a multiple of four.
The address must also be a multiple of four. A native DMA provider can impose
stronger alignment and aperture constraints.

The mapping must cover `pitch * height`, including the final padded row.
The entire span must fit below the 40-bit address limit. A CPU pointer or an
unmapped physical address is not a valid DMA mapping merely because it fits.
The geometry limits describe register fields, not supported HDMI modes.

The result contains explicit values for UCH0 layer zero. It is not a memory
image that can be copied over the register aperture. `U = DE + 0x1c1000`.

| Result | Register | Encoding |
| --- | --- | --- |
| attribute | `U+0x00` | `0xff000403`: enabled, global alpha 255, alpha mode 1, XRGB8888 code 4, no premultiplication |
| size | `U+0x04` | width minus one in 12:0; height minus one in 28:16 |
| coordinate | `U+0x08` | Zero origin |
| pitch | `U+0x0c` | Bytes per row |
| address_low | `U+0x10` | Low 32 DMA address bits |
| bottom_low | `U+0x14` | Zero for this progressive profile |
| fill_color | `U+0x18` | Zero; fill mode is disabled |
| address_high_byte | `U+0x80`, bits 7:0 | High 8 DMA address bits for layer zero only |
| bottom_high_byte | `U+0x84`, bits 7:0 | Zero for layer zero only |
| channel_size | `U+0x88` | Same packed size |
| coarse_horizontal, coarse_vertical | `U+0xf0`, `U+0xf8` | Zero for 1:1 scale |

The caller must own the layer and channel. It must preserve other layers'
address-high bytes and all unrelated fields. This encoder does not disable
other layers, compression, scaling, CSC, backend or output-format blocks.
It does not authorize enabling the attribute before those prerequisites.

## RCQ descriptor

Each descriptor is 16 bytes, in explicit little-endian word order:

| Word | Fields |
| --- | --- |
| 0 | Source block DMA address, low 32 bits |
| 1 | Byte count in 23:0; high 8 DMA address bits in 31:24 |
| 2 | Dirty flag in bit 0; other bits zero |
| 3 | Destination register offset from the DE base |

The source address must be 32-byte aligned. Byte count is nonzero, a
multiple of four, and at most `0xffffff`. The source range must fit within
40 bits. Destination and length must fit the 4 MiB DE aperture and use
32-bit alignment. These bounds do not authorize any particular register.
A native producer must apply its owned-block allowlist before submission.
The encoder does not accept an absolute CPU register address.

`sun60i_a733_de352_rcq_list_size` rounds a nonzero descriptor count up to an
even count. It returns the serialized size in a `uint32_t`, or `EOVERFLOW`
if that software value cannot hold the size. This bound does not establish
the writable width of the hardware list-length field. The pinned BSP writes
a `u32` to `DE+0x811c`, but it provides no complete field table here. A native
submitter must apply a separately supported list bound. The caller must
zero-fill a final unused descriptor. It must allocate, map and synchronize the full padded
list and every source block. C bitfield layout and host byte order are not
part of this interface.

All three functions leave their outputs unchanged on validation failure.
No encoded pointer, dimension or descriptor is a resource-ownership token.

## Submission boundary

DE3.52 uses RCQ updates, not the DE2 double-buffer latch. The first native
producer still needs accepted CCU/PCK ownership, DE IOMMU/NSI mapping,
channel/output ownership and a documented unused-block disable sequence.
It must preserve unrelated register fields when it constructs each source
block. It must not zero the display aperture or derive reset defaults from
this format encoder.

A retained submission must have bounded completion and fault handling.
DMA buffers cannot be freed after a timeout without proof that the engine
has stopped or been isolated. An old RCQ-finish flag must not complete a new
submission. RCQ finish at DE `+0x8104`, bit 2, uses W1C. TV0 vblank at
`+0x04`, bit 14, uses W0C; these events and acknowledgement rules differ.
The [display ownership core](a733-display-ownership.md) is another prerequisite.
HDMI timing, PHY, HPD, DDC/EDID and native KMS remain separate work.

## Sources and validation

Origin: EmberBSD, AI-assisted, original BSD-2-Clause implementation. The
hardware facts are pinned to Orange Pi BSP revision
`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`:

- [Overlay register layout](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/ovl/de_ovl_type.h)
  and [overlay platform](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/ovl/de_ovl_platform.c)
  define UCH0, layer fields and the 40-bit address layout.
- [Channel format mapping](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/de_channel.c)
  defines the XRGB8888 hardware code.
- [RCQ layout](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/de_base.h)
  and [DE allocation/mode selection](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_de/sunxi_de.c)
  define descriptor fields, block alignment, even list count and RCQ mode.

These GPL-2.0-or-later files supply register facts. No C struct, macro,
function or initialization sequence is copied into this BSD implementation.
The [manual source](a733-display-observer.md#sources-and-license-boundary)
defines the distinct TV0 vblank acknowledgement rule.

```sh
python3 ember/tools/a733-de352-contract.py
DE352_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    python3 ember/tools/a733-de352-contract.py
```

The fixture compiles the actual production bodies. Independent byte vectors
check the wire format. Negative cases cover full padded spans, every low
alignment bit, zero and excessive geometry, address/range wrap, aperture
bounds, list-count overflow and unchanged output on error. `CC`,
`DE352_SOURCE_ROOT`, `DE352_CFLAGS` and `TEST_RUNNER` select a host compiler,
source, flags or supported execution wrapper. Host checks do not establish
physical DMA, RCQ completion, a visible RGB image or HDMI acceptance.
