# Fixed BCM2712 HDMI0 timing payload

The [pure encoder](../bcm2712_display_hdmi0.c) produces
named controller payloads for progressive 1920x1080 at 60 Hz, RGB8 and 148.5 MHz.
The fixed timing is `1920/2008/2052/2200`, `1080/1084/1089/1125`, with positive
sync polarities and no pixel repetition. It has no I/O, callback, mutable state,
native wiring or sink policy. It does not activate a controller or establish scanout.
See the [first-frame boundary](bcm2712-display-contract.md#fixed-first-frame-design-boundary)
for the separate clocks, PHY, HVS, PV, ownership and stop/drain gates.

## Fixed payload

[`bcm2712_hdmi0_timing`](../bcm2712_display_hdmi0var.h)
has six named timing words, five typed RMW pairs and one future CLOCK_STOP word.
The fields do not form a register list or specify an application order.
The offsets below are primary-source facts for a future owner, not an access API.

| Field | Resource:offset | Word |
|---|---|---|
| `horza` | `16:0xec` | `0x0058c780` |
| `horzb` | `16:0xf0` | `0x0094002c` |
| `verta0` | `16:0xf4` | `0x05040438` |
| `verta1` | `16:0x100` | `0x05040438` |
| `vertb0` | `16:0xf8` | `0x00000024` |
| `vertb1` | `16:0x104` | `0x044c0024` |

VC5 timing fields apply to BCM2712 through its VC6 register map. They differ
from the legacy VC4 layout. Both VERTA words contain pulse5, front4 and active1080.
VERTB1 contains horizontal offset1100 even for this progressive profile.
Resource16 is the canonical HDMI0 core aperture, `0x107c701400/0x300`.

The future RMW operation is `(old & ~clear_mask) | set_bits`. Clear and set masks
can overlap. The set bits win. Unrelated bits must stay unchanged.

| Field | Resource:offset | Clear mask | Set bits |
|---|---|---|---|
| `scheduler` | `16:0xe8` | `0` | `0x8020` |
| `deep_color` | `16:0x18c` | `0x70f` | `0x200` |
| `gcp_word` | `16:0x198` | `0xffff` | `0x10` |
| `gcp_config` | `16:0x194` | `0` | `0x80000000` |
| `misc_control` | `16:0x114` | `0xf` | `0` |

Scheduler preparation sets MANUAL_FORMAT and IGNORE_VSYNC_PREDICTS. It does not
select MODE_HDMI. RGB8 uses GCP depth code0 and packing phase2. Pixel repetition
is zero. GCP enables packet generation and encodes CLEAR_AVMUTE; these are future
commands, not evidence of inactive hardware or known sink capabilities.

`clock_stop=0` is a future whole-word payload for resource17+`0xbc`.
Resource17 is the HDMI0 DVP aperture, `0x107c701000/0x200`. This payload releases
stopped clocks. It is not a stop, idle, reset or cleanup predicate. Never apply
these fields without a separate validated owner and sequence.

The numeric facts come from Raspberry Pi Linux
`a1a601d4857978d810175365550c4536701df4ed`:
[VC5 fields and packing bits](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L67-L110),
[timing and packing](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L1271-L1372),
[scheduler preparation](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi.c#L1509-L1517)
and the [VC6 map](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/gpu/drm/vc4/vc4_hdmi_regs.h#L437-L485).
The independent BSD encoder copies no GPL implementation code.

## Interface and limits

`bcm2712_hdmi0_encode_1080p60` returns positive `EINVAL` for NULL output.
A valid output receives all fields once. The caller owns output storage and
serializes access to it. There is no format, clock, register or mode selector.
The encoder does not read EDID, validate a sink or acquire an owner.

AVI, CTA capabilities, RGB quantization/CSC policy, HDMI/DVI selection, scrambling,
VID_CTL, FIFO activation, PHY programming and HVS/PV publication remain separate.
A base EDID block with an unread extension does not establish `is_hdmi` or QS.
Never clear BCM2712 VID_CTL.ENABLE. A future persistent owner must prove stop
and DMA drain before it releases buffers, mappings, clocks or callback storage.
These payloads establish no PLL lock, power, handoff, link, first frame or KMS.

## Actual-source contract

```sh
sh ember/tools/bcm2712-hdmi0-timing-contract.sh "$PWD"
HDMI0_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-hdmi0-timing-contract.sh "$PWD"
CC=aarch64--netbsd-gcc CFLAGS="--sysroot=$SYSROOT" HDMI0_COMPILE_ONLY=1 \
    sh ember/tools/bcm2712-hdmi0-timing-contract.sh "$PWD"
```

The contract includes the actual encoder and header. Independent literals check
all fields. Tests check RMW interpretation with zero, all-one and mixed old bits,
progressive VERTB1, polarity, totals, NULL, poisoned output, canaries and repeated
encoding. Focused mutants must compile before behavioral rejection. They change
production facts and masks. The fixture does not execute or prove MMIO ordering.
`HDMI0_OUTPUT`, `HDMI0_EVIDENCE` and `TEST_RUNNER` preserve outputs and support
memory checkers on supported hosts. A separate actual `_KERNEL` TU compile checks
selected EmberBSD headers with GCC16. Source contracts are not physical acceptance.
