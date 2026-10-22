# Orange Pi 6 Plus

Experimental diagnostic target: CIX P1 CD8180, 32 GB, A4 PHECDA-Rev A.
Firmware: stock `BiosV1.5-W20260706-e321056a`.
This profile records one physical sample. It is not a complete installation image.

| Function | Result and limits |
| --- | --- |
| Boot | The installed UART2 kernel `f84fcb5ef` passes one automatic cold default boot and one automatic warm default boot with the reduced GCC16 SD/FFS root. A RAM recovery entry remains available. See the [persistent-root check](../image/cix-p1.md#physical-reboot-check). |
| Console | DEBUG UART2, 115200 8N1. The stock SPCR points at UART3. The loader correction selects UART2 without rewriting the firmware. A separate [manual kernel gate](../boot/cix-spcr.md#manual-kernel-console-profile) passes a manual cold boot and an automatic cold default boot with SPCR absent, with native plcom2 at 115200 8N1. |
| CPU and memory | The kernel reports 12 CPUs and about 32 GB. SMP stress and memory integrity are not qualified. |
| Instrument interfaces | The separate [native UART1/3 profile](../boot/cix-uart.md) attaches `plcom1`/`plcom3` using checked functional clocks264/266 at 100 MHz. Same-open native termios checks pass UART1 at 230400 8N1 and UART3 at 9600/115200 8N1; wire traffic is unqualified. DEBUG UART2 is unchanged. The separate [GPI5 input profile](../boot/cix-gpio.md#manual-fixed-gpi5-inputs) exposes GPIO015/016/017 through native `gpio0`: all three initial samples are1, output-changing requests return `EBUSY`, and out-of-range reads return `EINVAL`. External transitions, GPIO IRQ and PPS remain unqualified. [EMBER64_CIX_PERIPHERALS](../boot/cix-uart.md#combined-manual-peripherals-profile) composes both with VPU0; passes a manual warm boot, the same GPIO reads/denials and same-open UART termios checks. Automatic default warm boot also passes. Cold boot, external signals and instrument traffic remain unqualified. |
| Ethernet | Both RTL8126 ports negotiate 5GBASE-T full-duplex on one cable. The [finite raw-wire probe](../boot/rge-wire-probe.md#physical-finite-wire-check) passes 4096 alternating frames on kernel `24b6895ec`: 2048 TX/RX per port and 512 frames per length in each direction (60/1514/1515/9014 bytes excluding FCS). Payload/FCS checks pass, BPF drops are 0, and native input/output error counters do not change. At least two ring revolutions are exercised. The operator restores MTU 1500 and the original administrative/IPv4 state; the Wi-Fi default route is unchanged. IP/TCP on both ports, throughput, camera traffic and sustained-load qualification remain pending. Private single-segment jumbo DMA with independent mbuf copyout preserves MTU 1500/non-R26 paths. [Source fault tests](../tools/rge-rx-contract.c) cover allocation and lifetime errors; physical failure recovery remains unqualified. |
| Video codec | The default [ACPI attachment](../boot/cix-vpu.md) queries resources. Manual GCC16 diagnostics pass clock67 enable/disable, identity and the [seven-word profile](../boot/cix-platform.md#manual-native-hardware-profile), with checked control cleanup. CPU preparation audits 256 pages and releases both never-exposed owners. A [physical bootstrap trial](../boot/cix-vpu.md#physical-diagnostic-boundary) with kernel `c2e7cdcac` completes first ALLOC, fixed 512 KiB RESIZE and second 16 KiB ALLOC: three commits/notifications, SWITCHED_IN and PONG, primary 0. The closed plan maps 512 KiB for the first object and four separate pages for the second, retaining 64 MiB virtual capacity for the first. Exposed pages and claims remain held, IRQ masked. That physical bootstrap submits no codec frame. The separate [manual first-frame source profile](../boot/cix-vpu.md#manual-first-frame-experiment) selects bounded 128×128 NV12 setup, opaque queues and immutable host-copy export; native execution and independent decode are pending. General session commands, safe DMA teardown, data correctness, video queues, rails, waveform and fresh repair remain unqualified. |
| Remote access | The reduced native SSH extension passes root password authentication, PTY access, wrong-password rejection and automatic startup after a warm reboot and one cold default boot. The cold trial also passes 2 MiB file transfers each way with matching SHA256. Host identity and account databases persist. Access is administrator-provisioned; images contain no credentials. See [SSH provisioning](../image/cix-p1.md#optional-ssh-access). |
| Power button | Physical short presses reach native `powerd`, sync and FFS unmount. The operator measures about 5 s to fan stop. Two further short presses on the combined peripherals kernel also reach clean root FFS unmount and operator-confirmed fan stop. A sustained reliability run remains unqualified. A commanded reboot reaches FFS unmount in 0.31 s from its reason log; a new press-to-fan measurement remains pending. Warm boots report clean FFS and restore WPA2/DHCP. Restart by short press remains unresolved, including after UEFI-only shutdown: power must be removed and restored. The UART session leader passes native Ctrl-C, stopped-job and termination-without-input checks. EMBER64 selects the bounded pin6 ACPI event path; `flags 0x0` retains passive mode. See [CIX GPIO](../boot/cix-gpio.md). |
| RTL8822CE | Physical kernel `71877588ca099bf69686a35b96a00771a26feebc` passes WPA2-PSK/CCMP and fresh DHCP on channels 8 and 149. Each band passes simultaneous 16 MiB RAM-root TCP transfers in both directions with matching SHA256. The persistent SD profile also passes automatic reboot recovery and 2 MiB checks each way. See the [source boundary and contracts](../development/rtl8822ce-proposal.md#current-source-boundary). This is a legacy 802.11a/b/g profile with software CCMP. Cold calibration retains ADC AB and DAC B residual-quality warnings. A later GCC16 kernel at `e25f25939` passes legacy AUTO on one 2.4 GHz AP: 1 MiB raw TCP downloads reach about 25 Mbps, all four ACs deliver 12/12 ICMP replies, and six controlled reconnects restore IPv4 in 12–28 seconds, with LAN link-local IPv6 data after each cycle. GCC16 kernel `76ccc8959` additionally passes actual CCK/OFDM PHY reports and two controlled reconnects with fresh signal generations. GCC16 kernel `d175df270` passes two disruptive manual scans with automatic WPA2/IPv4 recovery in 26/34 seconds and LAN link-local IPv6 SSH after each. Four-AC probes and 1 MiB SSH/raw-IPv6 transfers pass afterward. Managed-mailbox kernel `d5b249d0b` also passes clean warm boot, two automatic scan recoveries (27/26 seconds), link-local IPv6 and four-AC/data regression checks. RX BA foundation kernel `6decb0f51` passes legacy boot, scan recovery (27/12 seconds), IPv6 and bounded data/queue checks. The first queue probe fails on a diagnostic `ENOMEM`; a separately recorded bounded recheck passes. Software RX BA does not enable hardware BA or HT. GCC16 kernel `4f152fc21` adds posted-DMA drain and stats resize handling. It passes scan recovery (26/33 seconds), IPv6, four queues, 100 diagnostic reads and bidirectional 1 MiB data checks. Kernel `9c1de345a` retains the pinned default-ACK register bit and passes legacy scan recovery (26/12 seconds), IPv6 and four-AC/data regression. This does not prove hardware BlockAck. Kernel `3d9255b15` corrects initial channel selection and passes 26/26-second scan recoveries with IPv6. Its 11a trial reaches channel 149 without the earlier EINVAL, but authentication still times out. Explicit AUTO restoration returns data in 27 seconds. Source contracts cover an HT20/one-stream AUTO rate path, but the native provider remains NULL. The channel 149 AP's HT Operation `0x0006` is now covered by the typed mixed-format host policy for operation `0`/`0x0006`; physical HT and 5 GHz data on that AP remain unqualified. This does not qualify calibrated dBm, full AP-loss recovery, HT/VHT, upper 5 GHz on that AP, analog quality or endurance. |
| Storage | SD supplies the boot payload and a new 2 GiB writable FFS root. Full image readback and forced native fsck pass before provisioning. The existing EFI and boot partitions remain. NVMe installation is not accepted. |
| Application runtime | The installed reduced static C base supports the checked diagnostic tools. A coherent C++ application environment, pthread and shared runtime closure are not installed or qualified on this sample. |
| USB | The changed GCC16 kernel boots with 16 nonempty logical USB root buses. Native usbdevs reads two attached devices by VID/PID (05e3:0761 storage and 0bda:c822 Bluetooth). Device-name conversion remains invalid in the reduced static base; its Citrus modules and data closure are missing. External USB3 devices, Bluetooth traffic and sustained transfers remain unqualified. See the [source check](#usb-root-bus-source-check). |
| Other peripherals | General-purpose GPIO/PPS, camera capture, Bluetooth, HDMI, GPU, NPU, thermal behavior and sustained operation are not accepted by this trial. |

## Boot and recovery

Use the [GCC16 kernel procedure](../development/kernel.md).
Keep a known working kernel, its matching RAM root and UART recovery available.
The [UART2 kernel profile](../boot/cix-spcr.md#manual-kernel-console-profile)
uses an exact DSDT gate when SPCR is absent. Its installed default passes one
automatic cold boot without changing firmware settings. The loader correction
remains applicable when the checked original SPCR exists. UART1 and UART3
remain separate instrument interfaces; native termios setup passes in their
manual profile, while wire baud and instrument exchanges remain unqualified.

The [opt-in profile](../boot/cix-p1-persistent.cfg) clears initrd and selects
`netbsd.wifi.gz` with `root=NAME=ember-cix-root`. The current development media
uses the UART2 kernel `f84fcb5efef545db3f148035873a99ad23d7fe60`, gzip SHA256
`5f216069b8100d2ff6f52d05fe87fc849481432d047d856beccaf4d91b0854b2`.
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
The current default also reaches the root without menu input after a full DC
cycle. FFS is clean and WPA2/CCMP, SSH/PTY and checked file transfers recover.
Association is initially pending; immediate network availability is not established.
Unclean power-loss recovery, long-term reliability, RTC and hardware detection
errors remain unqualified.

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
