# Propose and accept a board change

Use this worksheet within [CONTRIBUTING](../../CONTRIBUTING.md), including when a maintainer develops the port.
Keep one change proposal in the accepting project's supported channel.
For EmberBSD, use the draft proposal PR described in [CONTRIBUTING](../../CONTRIBUTING.md#2-describe-the-change-before-implementation).
Link related PRs to it. Do not create a separate procedure for each author.

## Proposal worksheet

Fill these fields before implementing the first port:

| Field | Record |
| --- | --- |
| Identity | Board model and revision, SoC SKU, RAM, storage, attached peripherals |
| Existing state | Current OS support, relevant upstream sources and their exact revisions |
| Firmware and handoff | Board-specific firmware version/hash, UEFI or other loader, DT/ACPI interface |
| Console | Verified connector, voltage, pinout, serial settings and observed boot log |
| First scenario | The smallest useful boot/storage/network result and its measurable acceptance criteria |
| Missing interfaces | Attachments, clocks/resets, interrupt/DMA/IOMMU paths and firmware assets requiring work |
| Build outputs | Configuration, kernel/modules, firmware/DTBs when applicable, image layout and boot selection |
| Shared regression | Existing platform and checks that can detect damage to common code |
| Recovery | Known accepted media, return path, and authorized deployment scope |
| Review | Implementer and independent reviewer; source, build and hardware evidence responsibilities |

Unknown fields remain unknown. Obtain evidence rather than substituting a similar board's values.
Agree the first scenario and exclusions before driver implementation.
Keep external documentation and Linux code as reference; preserve licensing boundaries.

## Acceptance stages

| Stage | Required result |
| --- | --- |
| Proposal | Owner, target identity, scope, baseline, recovery and first acceptance scenario recorded |
| Source | Reviewed kernel/driver and build/boot integration, with regression coverage |
| Build | Clean pinned build, compiler identity and coherent artifact hashes |
| First boot | Observed control transfer to the actual kernel and a recoverable console |
| Useful operation | Stated memory/storage/network scenario with checked data, errors and duration |
| Additional support | Separate measured results for SMP, USB, graphics, radio, audio, power or accelerators |

The next stage can investigate a failure without claiming the previous stage passed.
Missing physical access allows source/build work, but keeps physical acceptance open.
Use [adding a board](../boards/adding-a-board.md) for implementation ownership and the capability-table format.
Add a catalog entry only at its stated evidence level.
If source foundations merge early, preserve the remaining acceptance requirements in the change proposal.

## Applying the procedure to Orange Pi 6 Plus

Start with a board-change proposal, then fill the worksheet from the delivered device.
This is a support-assessment example, not a validated build or installation instruction.
Its author follows the same review and acceptance stages as any other contributor.
First test the current compatible EmberBSD build with the board's stock firmware.
If it works, document its hardware evidence; implement a patch only for a demonstrated gap.

The [vendor board definition](https://github.com/orangepi-xunlong/orangepi-build/blob/a95129bf30ea11886cc35f08ebb3e69695a26656/external/config/boards/orangepi6plus.conf)
identifies the CIX P1 family. Do not reuse the Allwinner Zero 4 boot path.
Determine the actual SoC SKU, board revision, RAM and stock firmware from the device.
Keep its stock firmware until a board-specific firmware change has its own recovery and acceptance plan.

The current AArch64 `GENERIC64` configuration contains ACPI, GICv3, PL011, PCIe, NVMe, `rge`, and xHCI attachments.
`EMBER64` includes that configuration. Source availability does not establish operation on this board.
The `rge` implementation matches Realtek RT8126; compare the delivered board's actual PCI IDs.
Check ACPI interrupt routing, PCI ranges and DMA/IOMMU requirements before treating those drivers as sufficient.

The [NetBSD Orion O6 report](https://mail-index.netbsd.org/port-arm/2025/01/19/msg009109.html)
shows an NVMe root on another CIX system with a restricted January 2025 kernel.
Its [follow-up analysis](https://mail-index.netbsd.org/port-arm/2025/01/19/msg009110.html)
identifies firmware runtime-map, xHCI initialization and interrupt-controller questions.
Use these as investigation leads. Do not copy another board's firmware or disable devices as a support claim.

Proposed first acceptance scenario:

1. Record board and stock firmware identity, ACPI/PCI data, console and recovery.
2. Select the accepted EmberBSD source, compiler and required firmware/loader inputs.
3. Build and inspect the coherent output before an authorized boot from external media.
4. Reach the kernel console and record actual memory and interrupt behavior.
5. Test storage on a designated disposable filesystem; verify written and read data by hash.
6. Test each claimed Ethernet port with checked transfers in both directions and a recorded duration.
7. Review the results and publish the exact capability limits.

Decide the transfer sizes, repetition count and workload in the proposal before measurement.
SMP, USB, thermals, GPU, NPU and other peripherals have separate acceptance scenarios.
Do not infer them from the first boot or a Linux result.

Coordinate a required loader dependency through its explicit handoff and artifact contract.
The common process does not wait for all parallel installer work.
An accepted amd64 installation image and network-source menu are not an Orange Pi installation image.
