# Board catalog

Start with the [board selector](../../README.md#supported-boards). Each board
has its own capability table, boot path, limitations and evidence. A new board
adds a row and a page, without adding columns to a project-wide feature matrix.

| Family | Board pages |
| --- | --- |
| Broadcom / AArch64 | [Raspberry Pi 5](raspberry-pi-5.md), [Compute Module 5](compute-module-5.md), [Raspberry Pi Zero 2 W](raspberry-pi-zero-2-w.md) |
| Allwinner A733 / AArch64 | [Orange Pi Zero 4](orange-pi-zero-4.md), [Orange Pi Zero 3W](orange-pi-zero-3w.md) |
| Espressif / RV32 | [ESP32-S31 development board](esp32-s31.md) |

## Read a support claim

| Term | Meaning |
| --- | --- |
| Source available | An implementation exists; neither a successful build nor device operation follows from that |
| Build checked | The recorded configuration compiled; physical operation remains unverified |
| VM checked | A named virtual-machine scenario passed; this does not test a physical board's peripherals |
| Tested | The stated function was exercised on physical hardware, within the recorded conditions |
| Not validated / not tried | No successful physical test is claimed; code may exist |
| No | The published table reports no implementation; “no port”, “no fan” and similar wording instead describe absent hardware |

Record failures and partial results explicitly. A boot, controller attachment
or pairing is narrower than a complete device workflow. Do not infer one board's
support from another board sharing its SoC. Sustained stability has not been
established for the listed boards.

The initial pages preserve an existing published matrix and cite its source
revision. Missing historical test metadata remains missing; moving documentation
does not create new hardware evidence.

## VM and research targets

The [UTM framebuffer workflow](../boot/utm-framebuffer.md) describes a virtual
AArch64 platform. It is separate from the physical-board catalog.
[GPU and NPU porting targets](../../README.md#gpu-foundations-and-npu-direction)
are research directions, not additional supported boards.
The [common board-change worksheet](../development/board-change.md#applying-the-procedure-to-orange-pi-6-plus)
uses Orange Pi 6 Plus as a first-port example. It has no accepted EmberBSD board profile yet.

## Contribute a board

Follow [adding a board](adding-a-board.md) for source ownership, build integration,
validation evidence and a reviewable contribution. Keep the board page current
when a test passes, a limitation changes, or support regresses. Update the short
README row only when that changes a developer's choice of board.
