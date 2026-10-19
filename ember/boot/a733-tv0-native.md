# A733 native TV0 and VO1 control

This mechanism uses checked 32-bit callbacks and the retained DISPLAY owner.
It does not attach a device or start output at boot.
The native provider must enforce MMIO widths, apertures and CCU/PCK ownership.
Call `sun60i_a733_tv0_route()` after the HDMI clock phase.
Call `sun60i_a733_tv0_prepare()` after the PHY and TV0 clock phases.
After the DE pipeline is enabled, call `sun60i_a733_tv0_start()`.
Observe TV0 vblank separately from DE RCQ completion.
A normal DE352 RCQ request needs active output timing.
Do not wait for RCQ completion before starting TV0.

## Finite write contract

All offsets are relative to the named MMIO window.
For masked writes, preserve bits outside the mask and check the selected bits.
The register map is A733 User Manual V1.00, sections 15.2.9 and 15.2.10.
The [timing encoder](a733-output-timing.md) defines the numeric timing fields.
The [clock phases](a733-output-clocks.md) preserve the separate clock lease.

| Phase | Window and offset | Mask | Value or condition |
| --- | --- | --- | --- |
| Route 1 | VO1 0x000 | 0x0000000a | 0: PHY clock0 and HDMI0 |
| Route 2 | VO1 0x020 | 0x10100000 | 0x10100000: TV0 and TV0 HDMI gates |
| Prepare 1 | TV0 0x08c | 0x0f000000 | All sync outputs disabled |
| Prepare 2 | TV0 0x338 | 0xffffffff | All lower data outputs disabled |
| Prepare 3 | TV0 0x33c | 0xffff0000 | All upper data outputs disabled |
| Prepare 4 | TV0 0x004 | Known RW/W0C fields | Write 0: disable local IRQ sources and clear pending flags |
| Prepare 5 | TV0 0x0fc | W0C error bits30/29 | Write 0: clear stopped-state underflow and lost-data errors |
| Prepare 6 | TV0 0x000 | 0x00000031 | 0: one pixel per clock, strict mode0; preserve reserved bit31 |
| Prepare 7 | TV0 0x040 | 0x00000001 | 0: DE input |
| Prepare 8 | TV0 0x090 | 0x000ffff0 | `min(vtotal - vdisplay - 5, 31) << 4` |
| Prepare 9 | TV0 0x098 | 0x8001ffff | `basic1`: doubled progressive vertical total; clear VIC39 |
| Prepare 10 | TV0 0x09c | 0x7fff7fff | `basic2`: active dimensions |
| Prepare 11 | TV0 0x0a0 | 0xffff7fff | `basic3`: horizontal total and back interval |
| Prepare 12 | TV0 0x0a4 | 0x00007fff | `basic4`: vertical back interval |
| Prepare 13 | TV0 0x0a8 | 0x0fff0fff | `basic5`: sync widths |
| Prepare 14 | TV0 0x088 | 0x0f000000 | Mode H/V polarity; normal DE and pixel-clock polarity |
| Prepare 15 | TV0 0x300 | 0x80000000 | 0: fill bypass |
| Prepare 16 | TV0 0x330 | 0xffffffff | 0: normal lower data polarity |
| Prepare 17 | TV0 0x334 | 0xffff0000 | 0: normal upper data polarity |
| Prepare 18 | TV0 0x340 | 0xc0ffff77 | 0: explicit vendor-zero experiment only |
| Start 1 | TV0 0x004 | Known RW/W0C fields | Write 0x40001f00: enable vblank and clear a stale vblank |
| Start 2 | TV0 0x338 | 0xffffffff | 0: enable lower data outputs |
| Start 3 | TV0 0x33c | 0xffff0000 | 0: enable upper data outputs |
| Start 4 | TV0 0x08c | 0x0f000000 | 0: enable sync outputs |
| Start 5 | TV0 0x090 | 0x80000000 | Set TV0 enable |
| Observe | TV0 0x004 | Known RW/W0C fields | Preserve enables, write one to other W0C flags, clear vblank bit14 |

Prepare refuses an active TV0 and vertical blanking of five lines or fewer.
Start also refuses an already active TV0.
Observe checks the running bit, error flags and vblank enable before acknowledgement.
It increments the sequence only after the pending bit clears on readback.
If a new frame arrives before that readback, the conservative result is an error.
It does not count an unproven event twice.
A callback failure or readback mismatch retains the first fault.
No later read, write or ownership callback runs after that fault.
The native owner and written resources remain retained.

## Experimental depth profile

The manual gives conflicting fields for offset 0x340.
It describes bits2:0 as PIXELDEPTH, with 000 for 6-bit and 001 for 8-bit.
It also describes bit0 as VRR_EN.
The pinned [vendor sequence](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/drm/sunxi_device/hardware/lowlevel_tcon/tcon_tv_reg.c)
uses zero for the complete register.
That sequence does not resolve the manual conflict.
The API therefore requires `A733_TV0_EXPERIMENT_VENDOR_ZERO` explicitly.
This profile does not establish an 8-bit or VRR encoding.
Physical output requires a reviewed experiment and independent frame evidence.
The implementation is independent BSD code and is AI-assisted.

## Source checks

Run `python3 -B ember/tools/a733-tv0-contract.py`.
Use `A733_TV0_CFLAGS` to add native sanitizer flags.
The test uses the actual timing and TV0 function bodies.
It checks the write sequence, every callback failure, every rejected write,
late W0C flags, reserved GCTL bit31, phase order and event accounting.
Source and target compilation do not establish physical HDMI output.
