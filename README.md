# EmberBSD

**Unix for intelligent devices.**

EmberBSD is an independent fork of [NetBSD](https://github.com/NetBSD/src), based on NetBSD 11.
It targets embedded devices and computers with a modular BSD system.
The product version is `EMBER_VERSION` in [share/mk/bsd.own.mk](share/mk/bsd.own.mk).
NetBSD compatibility identifiers remain separate. Use the
[SDK release procedure](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/release-tags.md)
to set the canonical source version and pin matching release inputs.

This repository owns the kernel, base userland, drivers, loader integration and their checks.
Applications, dependencies, hardware references and shared development tools have separate owners.
The OS does not require an AI agent, a model provider or a particular graphical shell.

## Purpose and getting started

- Select hardware and check its limits in [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).
- Install and configure the OS with [Quick Start](doc/guide/quick-start.md) and the [User Guide](doc/guide/user-guide.md).
- Build components with the [build guide](README.ember.md). Use [SDK](https://github.com/oxtech-ember/EmberBSD-SDK) for shared workspaces and release tooling.
- Develop a driver through [CONTRIBUTING](CONTRIBUTING.md), the [kernel guide](ember/development/kernel.md) and [adding a board](ember/development/adding-a-board.md).
- Choose a destination using [source and documentation ownership](doc/emberbsd-ownership.md).

## EmberBSD ecosystem

| Repository | Responsibility |
| --- | --- |
| [EmberBSD](https://github.com/oxtech-ember/EmberBSD) | OS mechanisms, native build targets and executable OS checks |
| [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) | Hardware, SoCs, loaders, installation applicability and current scoped support |
| [Ports](https://github.com/oxtech-ember/EmberBSD-Ports) | Package recipes, common dependencies, toolchains and portability patches |
| [SDK](https://github.com/oxtech-ember/EmberBSD-SDK) | Host workspaces, cross-build orchestration, sysroots and release kits |
| [Examples](https://github.com/oxtech-ember/EmberBSD-Examples) | Standalone applications and reproducible AI, robotics and desktop scenarios |
| [Spark](https://github.com/oxtech-ember/EmberBSD-Spark) | Optional C/Lua agent, model/MCP integration and task execution |
| [Runtime](https://github.com/oxtech-ember/EmberBSD-Runtime) | Planned application lifecycle, permissions and common device operations |
| [Developer skills](https://github.com/oxtech-ember/Ember-Agent-Skills) | Instructions for contributors and their AI assistants |
| [User skills](https://github.com/oxtech-ember/EmberBSD-User-Skills) | Installation, device operation and personal software changes |

Each owner documents implemented interfaces and remaining limits.
A repository name or plan does not establish a released capability.
SDK host tools exist; stable application SDK/Runtime contracts remain separate work.

## What this fork adds to NetBSD 11

The fork develops native ARM and RISC-V support, modern cross-build integration,
wireless networking, graphics and accelerator interfaces. Read the owning implementation:

| Area | Code and contracts |
| --- | --- |
| Allwinner display, PowerVR and NPU | [sunxi](sys/arch/arm/sunxi), [guides](sys/arch/arm/sunxi/docs) |
| BCM2712 display and V3D | [platform](sys/arch/arm/broadcom), [native DRM](sys/external/bsd/drm2/bcmv3d) |
| CIX platform and peripherals | [ACPI](sys/dev/acpi/docs), [console](sys/arch/evbarm/dev/docs) |
| Small-memory RISC-V | [Espressif port](sys/arch/riscv/espressif) |
| Wireless | [net80211](sys/net80211), [SDIO](sys/dev/sdmmc/docs), [drivers](sys/dev/ic/docs) |
| Display and synchronization | [DRM](sys/external/bsd/drm2/drm/docs), [VirtIO GPU](sys/external/bsd/drm2/virtio/docs) |
| Installation | [sysinst](usr.sbin/sysinst/docs), [SDK release tooling](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/os-release.md) |
| Debugging and tracing | [OS tracing](external/cddl/osnet/docs/dtrace-dwarf.md), [Ports toolchain](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain) |
| AI, computer vision and robotics | [Ports](https://github.com/oxtech-ember/EmberBSD-Ports), [Examples](https://github.com/oxtech-ember/EmberBSD-Examples) |

These are implementation areas, not universal device or workload acceptance.
Source checks, compilation, VM execution and physical operation have different limits.

## Supported boards

[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) is the canonical hardware catalog.
Read the exact board, carrier, SoC and loader records before choosing an image.
A result applies only to its stated firmware, configuration and scenario.
Booting a kernel does not qualify every peripheral or sustained operation.
The OS does not maintain a second hardware matrix.

## Connect user skills

Install [EmberBSD-User-Skills](https://github.com/oxtech-ember/EmberBSD-User-Skills#install-the-package-or-a-skill)
for installation, diagnostics, application use and personal Ports workflows.
The package supplies instructions, not an agent runtime or credentials.

## Connect developer skills

Install [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment)
for repository selection, board development, ports and checked contributions.
Both packages can coexist. Follow the package owner's current installation commands.
An assistant follows the same validation requirements as a human contributor.

## Wi-Fi status and remaining work

Use DevBoards for radio hardware, firmware applicability and physical limits.
Use driver and net80211 guides for protocol behavior and executable checks.
Do not transfer results between boards solely because radio or SoC names match.

## Provenance and licenses

[Source metadata](ember/source.json) records the upstream basis and adaptation provenance.
Imported code retains its licenses and authorship. Additional integration utilities use
[BSD-2-Clause](ember/LICENSE) unless a file states another license.
