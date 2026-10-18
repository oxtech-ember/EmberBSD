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
| Video codec | The default [ACPI attachment](../boot/cix-vpu.md) queries resources without activating the codec. A separate native clock67/identity trial succeeds: ID `0x56648002`, four cores, four slots and SVN `0xe0c1afe1`; checked controls are released. [CPU allocation/preparation](../boot/cix-vpu.md#manual-cpu-preparation-probe), initial DMA ownership and [typed boot helpers](../boot/cix-vpu.md#typed-boot-preparation-and-receive) pass source checks. Manual kernels link with GCC16. [Initially masked IRQ registration](../boot/cix-vpu.md#initially-masked-native-irq-registration) passes source checks; VPU delivery is pending. The [dedicated boot epoch](../boot/cix-vpu.md#dedicated-native-boot-epoch-source) passes source fault checks. An explicit [manual firmware profile](../boot/cix-vpu.md#manual-firmware-handshake-trial) invokes it after root mounts and retains ownership after every attempt; physical execution remains pending. The [seven-word hardware profile](../boot/cix-platform.md#manual-native-hardware-profile) has source/build checks; its physical trial is pending. Waveform, rails, fresh core repair, device-visible DMA, firmware execution and video queues remain unqualified. See the [native physical receipt](../boot/cix-platform.md#physical-native-clock67-and-vpu-identity-receipt). |
| Power button | Physical short presses reach native `powerd`, filesystem sync and FFS unmount. Adaptive utilities reduce the operator's press-to-fan-stop measurement from 15–20 s to about 5 s. UART records 4.05 s from the shutdown reason to FFS unmount. A later network-stop ordering fix reduces that interval to 0.31 s in one commanded reboot; new button-to-fan timing is unmeasured. The next unattended boot reports clean FFS and restores WPA2/DHCP with the marker and new utilities intact. Restart by short press remains unresolved: power must be removed and restored. The retained UART session leader passes native Ctrl-C, stopped-job and termination-without-input checks. EMBER64 selects the bounded pin6 ACPI event path; `flags 0x0` retains passive mode. See [CIX GPIO](../boot/cix-gpio.md). |
| RTL8822CE | Physical kernel `71877588ca099bf69686a35b96a00771a26feebc` passes WPA2-PSK/CCMP and fresh DHCP on channels 8 and 149. Each band passes simultaneous 16 MiB RAM-root TCP transfers in both directions with matching SHA256. The persistent SD profile also passes automatic reboot recovery and 2 MiB checks each way. See the [source boundary and contracts](../development/rtl8822ce-proposal.md#current-source-boundary). This is a legacy 802.11a/b/g profile with software CCMP. Cold calibration retains ADC AB and DAC B residual-quality warnings. Analog qualification, higher rates, AP-loss recovery and long-term stability remain pending. |
| Storage | SD supplies the boot payload and a new 2 GiB writable FFS root. Full image readback and forced native fsck pass before provisioning. The existing EFI and boot partitions remain. NVMe installation is not accepted. |
| USB | The changed GCC16 kernel boots with 16 nonempty logical USB root buses. Native usbdevs reads two attached devices by VID/PID (05e3:0761 storage and 0bda:c822 Bluetooth). Device-name conversion remains invalid in the reduced static base; its Citrus modules and data closure are missing. External USB3 devices, Bluetooth traffic and sustained transfers remain unqualified. See the [source check](#usb-root-bus-source-check). |
| Other peripherals | General-purpose GPIO/PPS, camera capture, Bluetooth, HDMI, GPU, NPU, thermal behavior and sustained operation are not accepted by this trial. |

## Boot and recovery

Use the [GCC16 kernel procedure](../development/kernel.md).
Keep a known working kernel, its matching RAM root and UART recovery available.
Enable `Support SPCR` in firmware. On this sample it can be lost after power removal.
Use the [CIX SPCR loader correction](../boot/cix-spcr.md); UART1 and UART3 remain separate device interfaces.

The [opt-in profile](../boot/cix-p1-persistent.cfg) clears initrd and selects
`netbsd.wifi.gz` with `root=NAME=ember-cix-root`. The current development media
uses kernel `522474e4de9f597153b5192ad579d900fff273ca`, gzip SHA256
`f1108160847844625ac9cce4d28419042687388c9f95d794d5b3341107aa1d1c`.
The retained `71877588c` rollback gzip SHA256 is
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
