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
| Video codec | [Passive ACPI attachment](../boot/cix-vpu.md) validates both memory windows, level IRQ 358 and `_CCA=0`. Power, clocks, reset, MMIO, DMA and video queues remain disabled. No hardware encoding is claimed. |
| Power button | The [GPI4 passive snapshot](../boot/cix-gpio.md#passive-physical-check) passes without an MMIO fault. EMBER64 now selects the bounded pin6 ACPI event path; `flags 0x0` retains passive mode. Physical button delivery and orderly poweroff remain unqualified. |
| RTL8822CE | Physical kernel `71877588ca099bf69686a35b96a00771a26feebc` passes WPA2-PSK/CCMP and fresh DHCP on channels 8 and 149. Each band passes simultaneous 16 MiB RAM-root TCP transfers in both directions with matching SHA256. The persistent SD profile also passes automatic reboot recovery and 2 MiB checks each way. See the [source boundary and contracts](../development/rtl8822ce-proposal.md#current-source-boundary). This is a legacy 802.11a/b/g profile with software CCMP. Cold calibration retains ADC AB and DAC B residual-quality warnings. Analog qualification, higher rates, AP-loss recovery and long-term stability remain pending. |
| Storage | SD supplies the boot payload and a new 2 GiB writable FFS root. Full image readback and forced native fsck pass before provisioning. The existing EFI and boot partitions remain. NVMe installation is not accepted. |
| Other peripherals | USB device failures remain in the boot log. General-purpose GPIO/PPS, camera capture, Bluetooth, HDMI, GPU, NPU, thermal behavior and sustained operation are not accepted by this trial. |

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
