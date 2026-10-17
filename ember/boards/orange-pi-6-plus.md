# Orange Pi 6 Plus

Experimental diagnostic target: CIX P1 CD8180, 32 GB, A4 PHECDA-Rev A.
Firmware: stock `BiosV1.5-W20260706-e321056a`.
This profile records one physical sample. It is not a complete installation image.

| Function | Result and limits |
| --- | --- |
| Boot | Stock UEFI and an SD FFS root pass five unattended warm boots with kernel `71877588c` and the reduced GCC16 userland. A RAM recovery entry remains available. See the [persistent-root check](../image/cix-p1.md#physical-reboot-check). |
| Console | DEBUG UART2, 115200 8N1. The stock SPCR points at UART3. The loader correction selects UART2 without rewriting the firmware. |
| CPU and memory | The kernel reports 12 CPUs and about 32 GB. SMP stress and memory integrity are not qualified. |
| Ethernet | `rge1` negotiates 1000baseT full-duplex. A short host-to-board transfer and ICMP replies pass. Board-initiated ping to the host did not receive replies; the cause is unconfirmed. The second port and sustained camera traffic remain unqualified. |
| Video codec | [ACPI attachment](../boot/cix-vpu.md) validates both memory windows, level IRQ 358 and `_CCA=0`. Native [clock/reset providers and a bounded resource probe](../boot/cix-platform.md) have source contracts. EMBER64 selects a resource query without VPU activation. An identity probe requires explicit flags; DMA, firmware and video queues are incomplete. No hardware encoding is claimed. |
| Power button | Two short presses reach native `powerd`, filesystem sync and FFS unmount. The operator confirms fan stop and a subsequent short-press power-on; the next boot reports clean FFS. The observed shutdown takes about 15–20 seconds. Reduction toward 3–5 seconds remains pending, with no timing guarantee. EMBER64 selects the bounded pin6 ACPI event path; `flags 0x0` retains passive mode. See [CIX GPIO](../boot/cix-gpio.md). |
| RTL8822CE | Physical kernel `71877588ca099bf69686a35b96a00771a26feebc` passes WPA2-PSK/CCMP and fresh DHCP on channels 8 and 149. Each band passes simultaneous 16 MiB RAM-root TCP transfers in both directions with matching SHA256. The persistent SD profile also passes automatic reboot recovery and 2 MiB checks each way. See the [source boundary and contracts](../development/rtl8822ce-proposal.md#current-source-boundary). This is a legacy 802.11a/b/g profile with software CCMP. Cold calibration retains ADC AB and DAC B residual-quality warnings. Analog qualification, higher rates, AP-loss recovery and long-term stability remain pending. |
| Storage | SD supplies the boot payload and a new 2 GiB writable FFS root. Full image readback and forced native fsck pass before provisioning. The existing EFI and boot partitions remain. NVMe installation is not accepted. |
| USB | Earlier boots report zero-port USB3 root hubs on four USB2-only controllers. The ACPI frontend now skips empty logical root buses in source contracts. The changed kernel and external USB devices remain unqualified. See the [source check](#usb-root-bus-source-check). |
| Other peripherals | General-purpose GPIO/PPS, camera capture, Bluetooth, HDMI, GPU, NPU, thermal behavior and sustained operation are not accepted by this trial. |

## Boot and recovery

Use the [GCC16 kernel procedure](../development/kernel.md).
Keep a known working kernel, its matching RAM root and UART recovery available.
Enable `Support SPCR` in firmware. On this sample it can be lost after power removal.
Use the [CIX SPCR loader correction](../boot/cix-spcr.md); UART1 and UART3 remain separate device interfaces.

The [opt-in profile](../boot/cix-p1-persistent.cfg) clears initrd and selects
`netbsd.wifi.gz` with `root=NAME=ember-cix-root`. The checked gzip SHA256 is
`2902b98e8b604446ec7103c9efe7e1fe928ad8a07d0711cdce2ebb37b514766d`.
The recovery menu selects the retained `/netbsd` and `root.fs` pair.
The [root guide](../image/cix-p1.md) records userland, startup and image pins.
These aliases describe the checked development media, not a release layout.
Do not write arbitrary storage devices or reuse another board's firmware.

The VPU resource contract passes 50 assertions on the physical kernel.
Source contracts, GCC path analysis, host sanitizers and Linux CI memory checks
do not qualify firmware execution, live DMA or a usable codec.
Earlier firmware DDR training/BIST messages still require separate investigation.
A later Wi-Fi kernel trial waited for `acpibat0` before root attachment after
an ACPI `AE_NO_MEMORY` error. A warm retry reached the shell without a source change.
Boot reliability and the cause of this failure remain unqualified.
The installed profile queues `userconf disable acpibat` on each boot.
This makes the diagnostic policy repeatable; it does not repair battery ACPI.
All five warm boots reach the disk root without interactive commands.
Cold power removal remains untested. The RTC warning and hardware detection
errors remain visible in the boot log.

For reusable board and interface references, start with
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).
Apply the [board contribution procedure](adding-a-board.md) to each new support claim.

## USB root-bus source check

Earlier diagnostic boot logs report `0 ports` for USB3 root hubs on four
USB2-only controllers. The corresponding `root device is not a hub` messages
do not prove failed external-device transfers. The ACPI frontend previously
attached both logical buses even when the core reported no ports for one bus.

`xhci_acpi` now attaches each logical USB bus only when its initialized
`sc_rhportcount` is nonzero. This is the count used by the root-hub descriptor.
No controller address, board ID, speed override or clock/reset change is used.
The [actual-source contract](../tools/xhci-acpi-rootbus-contract.c) passes
584 checks with GCC16 path analysis and ASan/UBSan. Five causal mutations fail.
The [CIX platform workflow](../../.github/workflows/cix-platform.yml) runs the
same frontend model with GCC16, Cppcheck, CSA, sanitizers and Linux Memcheck.
These CI checks use explicit host services. They do not execute native MMIO,
DMA, AML or hardware interrupts.

An actual AArch64 GCC16 kernel-flags compilation also passes. Native-header
analysis retains one inherited Cppcheck warning and four CSA header dead stores.
Cppcheck 2.22 cannot parse a direct-header `__aligned` expansion; actual GCC
preprocessing permits comparison with the same baseline findings.

This fix has not yet run on the physical sample. External-device enumeration,
transfers, hotplug and suspend/resume remain unqualified. If both port counts
are zero, the frontend reports an error and creates no children. This does not
repair the existing core IRQ assertion when no child exists. Both child attach
attempts returning NULL can reach the same gap. Core start/stop behavior and
the frontend's existing failure-cleanup limits are unchanged.

Run the frontend contract and host analysis with GCC16:

```sh
CC=gcc-16 CFLAGS='-O2 -g -fanalyzer' XHCI_ACPI_ANALYZE=1 \
    sh ember/tools/xhci-acpi-rootbus-contract.sh
```
