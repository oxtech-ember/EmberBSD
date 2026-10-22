# Adding a board to EmberBSD

This guide is for developers contributing another physical board. Start from
the closest implemented platform, then establish what is actually reusable.
A matching CPU or SoC does not establish matching firmware, wiring or peripherals.
Follow [CONTRIBUTING](../../CONTRIBUTING.md) and open a board-change proposal first.
Use [the common worksheet](board-change.md) to agree scope and acceptance.
These requirements also apply when the project owner implements the port or an agent writes the patch.

## Identify the target and existing support

Record the board model and revision, SoC, RAM, boot storage, serial console,
firmware chain, device-tree or ACPI interface, and available vendor documentation.
Identify the intended first scenario, such as boot to multi-user with Ethernet.
Inspect current EmberBSD and upstream NetBSD sources before writing a driver.
Preserve upstream identifiers, licenses, revision and authorship when adapting code.

For reusable hardware references, read
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).
Start with its [reader guide](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/docs/reader-guide.md) and [catalog](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/catalog.json), then read the exact board and carrier records.
Follow the selected bootloader contract and design transfer notes.
Check source revisions and configuration applicability before reuse.
Keep board, carrier, SoC, and bootloader knowledge at their respective layers.
Drivers and executable checks remain here or in Ports.

Read [the build guide](../../README.ember.md) and the exact DevBoards hardware profile.
Use a contributor branch or fork and the target's current submission rules.
The [developer skill](https://github.com/oxtech-ember/Ember-Agent-Skills#add-your-board)
can guide an assistant through the same public workflow.

## Integrate with the actual source tree

| Change | Owner / starting point |
| --- | --- |
| SoC, bus and device drivers | `sys/arch/` and `sys/dev/`, following existing attachment and configuration conventions |
| Kernel configuration | Relevant `sys/arch/*/conf`; inspect `EMBER64` and the `ESP32S31*` configurations as examples, not universal templates |
| Device tree | Existing platform DTS sources and build integration; A733 sources currently live under `sys/external/gpl2/dts/dist/arch/arm64/boot/dts/allwinner/` |
| Boot selection and image assets | `ember/boot/`; preserve upstream bindings and account for firmware licenses and hashes |
| Raspberry Pi UEFI adaptation | Pinned firmware inputs and ordered patches in `ember/firmware/`; this is not a generic firmware builder for all boards |
| Build and regression checks | Applicable build entry point and `ember/tools/` |
| Hardware profile, loader/layout applicability and scoped physical support | [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards); update the existing board, SoC or loader record |
| Software API, ownership, failure semantics and executable checks | The implementation directory in this repository |
| Third-party application dependencies | [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports) |
| Standalone device demonstration | [EmberBSD-Examples](https://github.com/oxtech-ember/EmberBSD-Examples) |

Adaptations already present in this fork are applied to the sources. Do not
reapply `ember/patches`. Firmware's ordered series is a separate input to its
builder. Keep fixes in versioned sources, not only in a build VM.

The current AArch64 kernel wrapper accepts a configuration, but explicitly
lists its device trees and modules. Adding a DTS or a configuration alone does
not add its outputs to a bootable bundle. Extend the relevant build, image and
selection paths and their checks. Existing native contracts use upstream Python;
new project-owned utilities and tests follow the repository's language rules.

Use the [documented build commands](../../README.ember.md#building) appropriate
to the architecture. Record exact inputs and output hashes. Deploy a coherent
kernel, modules, device tree, firmware and userland; do not silently reuse outputs
left by a failed build. Board-specific flash layout and recovery instructions
belong in the DevBoards profile once verified.

## Validate incrementally

Start with a recoverable boot and serial log, then memory/storage and a useful
network or device scenario. Exercise the interfaces the contribution claims:
for example, bidirectional transfers with checksums, watchdog reset, thermal
behavior under load, or audio capture/playback. A controller attaching is not
proof that its attached device works. Record cold boot, reboot and recovery
where relevant, and the actual test duration and workload.

Select source contracts and regressions for the changed subsystem. Check shared
paths on an existing supported configuration so a new board does not silently
break another one. A VM can help test common code, but cannot validate a physical
PHY, radio, GPIO pin, camera, power rail or accelerator.

Flashing and rebooting hardware must be within the contributor's task scope.
Before writing a physical disk, identify the device and capacity and document
a recovery path. Keep keys, network credentials, bonds and personal images out
of commits and public logs.

## Record the result

Update the canonical DevBoards profile and its scoped evidence. Record board/revision,
firmware, exact inputs, scenario, duration, measurements and untested functions.
Keep hardware capability separate from source, build, VM and physical results.
Unknown metadata stays unknown. Do not create a second support table in this OS.

Document API contracts and reproducible software checks beside their implementation.
Store a result and its limits, not a diary of attempts or reviewer conversations.
A source commit identifies documentation only unless the tested artifact is bound to it.

Record the artifact actually selected by the loader. With QEMU direct kernel
boot, the host's `-kernel` input can differ from the guest's `/netbsd`; hashing
that guest file alone does not identify the running kernel.

## Submit the contribution

After relevant checks, submit a focused change for independent review through CONTRIBUTING,
with code, build integration, regression coverage and board instructions.
Update the DevBoards catalog when the hardware profile changes.
Keep the OS README as a short navigation page; link the owner instead of copying its matrix. Explain the resulting scenario, provenance, tests and
remaining gaps. If required hardware validation is unavailable, describe it
plainly and use a draft when the target workflow permits one.

Prepare generic upstream changes separately and follow the receiving project's
current submission and AI-assistance rules. An EmberBSD PR is not proof of
acceptance by NetBSD or a firmware upstream.
