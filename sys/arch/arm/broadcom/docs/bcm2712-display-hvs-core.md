# Fixed BCM2712 HVS plane encoder

The [pure encoder](../bcm2712_display_hvs.c) produces one
fixed channel list. It has no native wiring, allocation, callbacks or MMIO.
It does not publish a list or enable HVS. It does not establish working scanout.
See the [first-frame boundary](bcm2712-display-contract.md#fixed-first-frame-design-boundary)
for the separate clock, mapping, handoff and stop/drain gates.

## Fixed payload

The profile is one immutable 1920x1080 linear XRGB8888 frame. Its pitch is 7680
and its byte size is 8294400. The source and output start at 0,0. There is no
crop, scaling, reflection, CSC or blending. Opaque alpha ignores the X byte.
The record uses explicit pixel-blend NONE. C0 and D0 encode fixed alpha in
different fields.

[`bcm2712_hvs_dlist`](../bcm2712_display_hvsvar.h)
holds ten numeric `uint32_t` words. It is not a packed byte stream or a generic
register-write interface.

| Index | Meaning | C0 | D0 |
|---:|---|---|---|
| 0 | CTL0: valid, NEXT9, unity, ARGB order, RGBA8888 | `0x4900c007` | `0x490cc007` |
| 1 | POS0: x0, y0, no flip | `0x00000000` | same |
| 2 | CTL2: opaque alpha, no CSC, mix or premultiply | `0x4000fff0` | `0x0000fff0` |
| 3 | POS2: height minus 1, width minus 1 | `0x0437077f` | same |
| 4 | Context initial seed | `0xc0c0c0c0` | same |
| 5 | PTR0: UPM base, slot and DMA upper byte | formula below | same |
| 6 | PTR1: DMA low 32 bits | formula below | same |
| 7 | PTR2: raster pitch | `0x00001e00` | same |
| 8 | Plane-emitted END word | `0x80000000` | same |
| 9 | Channel terminator | `0x80000000` | same |

The plane record has nine words including index 8. The channel list adds index 9.
CTL0.NEXT is 9. Unity RGB needs no LBM, scaling kernel, palette or YUV words.
C0 uses CTL2 fixed-alpha mode 1. D0 uses CTL0 alpha-mask 3 instead.
Both use fixed alpha 4095. The context seed has no lock or readiness meaning.
HVS changes context in its SRAM copy. Do not reapply this initial template to
an active list.

The numeric facts come from Linux revision
`a1a601d4857978d810175365550c4536701df4ed`:
[format mapping](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L33-L47),
[alpha rules](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L1305-L1354),
[plane words and NEXT](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L2181-L2344),
[bit fields](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_regs.h#L1381-L1437)
and the separate [channel terminator](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1309-L1346).
The BSD implementation is independent. It does not copy GPL implementation code.

## Input and lifetime contract

`bcm2712_hvs_encode_1080p60` accepts an existing `bcm2712_hvs_frame` grant and
one exact output type. It returns positive BSD errors in this order:

- NULL input or output: `EINVAL`.
- Variant other than `BCM2712_HVS_C0` or `BCM2712_HVS_D0`: `EOPNOTSUPP`.
- Invalid DMA or UPM metadata: `EINVAL`.

Every rejection leaves output unchanged. Success commits all ten words once.
The input is unchanged. The caller owns input/output storage and serializes it.
There is no mutable encoder state, retry, owner acquisition or cleanup operation.

`dma_base` is an HVS-visible DMA address. It is separate from a V3D virtual
address. `dma_span` must contain at least 8294400 bytes. The complete supplied
span must fit below 2^36. Address 0 is permitted. An exclusive end of 2^36 is
permitted. Validation uses subtraction and does not truncate or wrap addresses.
The [pinned driver selects a 36-bit DMA mask](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_drv.c#L327-L339)
even though PTR0 has an upper byte. The encoder adds no base-alignment rule.
Metadata does not prove mapping, physical contiguity, IOMMU state, coherency,
producer completion or protection from fetches outside the documented frame.

`upm_offset` and `upm_length` describe a reservation in bytes. Both must be
multiples of 256. The length must be at least 15616. The complete grant must
fit in the conservative 262144-byte arena. `upm_slot` is a hardware slot from
0 through 31. It is not a Linux allocator handle from 1 through 32.

PTR0 is `(upm_offset / 256) << 16 | upm_slot << 10 | dma_base >> 32`.
PTR1 is the low 32 bits of `dma_base`. The two-line UPM selector is 0.
VFLIP and reserved fields stay clear. The pinned
[UPM calculation](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L901-L926)
and [allocation packing](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_plane.c#L1132-L1205)
use two lines and 256-byte units. This gives 15616 bytes for pitch 7680.
The [arena size](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hvs.c#L1711-L1721)
is a pinned driver policy. It does not prove current hardware capacity or free
space. The caller must reserve the slot and range exclusively.

The caller must keep the frame, mappings, UPM slot/range and dlist SRAM region
until stop and DMA drain are proved. The pure encoder does not validate those
lifetimes, capacity, power, clocks, firmware handoff, sink mode or publication.
It emits no channel-register or LPTR payload. MOP and TXP writeback are excluded.

## Actual-source contract

```sh
sh ember/tools/bcm2712-hvs-plane-contract.sh "$PWD"
HVS_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-hvs-plane-contract.sh "$PWD"
CC=aarch64--netbsd-gcc CFLAGS="--sysroot=$SYSROOT" HVS_COMPILE_ONLY=1 \
    sh ember/tools/bcm2712-hvs-plane-contract.sh "$PWD"
```

The contract includes the actual encoder and header. It checks independent
literal records, both variants, DMA upper bits and full-span boundaries, UPM
bounds/slots, error precedence, NULL, input preservation, poisoned output,
canaries and repeated calls. Mutants change used production fields or predicates.
Compile failure does not count as behavioral mutant rejection. `HVS_OUTPUT` and
`TEST_RUNNER` support preserved outputs and supported memory checkers.
These checks establish a software contract, not a first frame or working link.
