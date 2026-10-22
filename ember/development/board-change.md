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
Use [adding a board](adding-a-board.md) for implementation ownership and the DevBoards evidence route.
Update the DevBoards profile only at its stated evidence level.
If source foundations merge early, preserve the remaining acceptance requirements in the change proposal.
