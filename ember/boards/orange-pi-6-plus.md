# Orange Pi 6 Plus

Experimental diagnostic target: CIX P1 CD8180, 32 GB, A4 PHECDA-Rev A.
Firmware: stock `BiosV1.5-W20260706-e321056a`.
This profile records one physical sample. It is not a complete installation image.

| Function | Result and limits |
| --- | --- |
| Boot | Stock UEFI, SD boot payload and an FFS RAM root reach the diagnostic shell. The 2026-10-17 trial used `EMBER64` from `293e5f1166569113072a1deac12012e025056ce0`. |
| Console | DEBUG UART2, 115200 8N1. The stock SPCR points at UART3. The loader correction selects UART2 without rewriting the firmware. |
| CPU and memory | The kernel reports 12 CPUs and about 32 GB. SMP stress and memory integrity are not qualified. |
| Ethernet | `rge1` negotiates 1000baseT full-duplex. A short host-to-board transfer and ICMP replies pass. Board-initiated ping to the host did not receive replies; the cause is unconfirmed. The second port and sustained camera traffic remain unqualified. |
| Video codec | [Passive ACPI attachment](../boot/cix-vpu.md) validates both memory windows, level IRQ 358 and `_CCA=0`. Power, clocks, reset, MMIO, DMA and video queues remain disabled. No hardware encoding is claimed. |
| RTL8822CE | Native PCI/net80211 station sources and GCC16 checks are integrated; see the [source boundary and contracts](../development/rtl8822ce-proposal.md#current-source-boundary). A physical `EMBER64` trial at `332dfb58356ffc23ef6642fc18691896d9c85b93` attached `rtwce0`, but `ifconfig rtwce0 up` timed out. Association on 2.4/5 GHz, WPA2, DHCP, data transport and reconnect remain unaccepted. |
| Storage | SD supplies the boot payload. The root is RAM only. NVMe installation is not accepted. |
| Other peripherals | USB device failures remain in the boot log. GPIO/PPS, camera capture, Bluetooth, HDMI, GPU, NPU, thermal behavior and sustained operation are not accepted by this trial. |

## Boot and recovery

Use the [GCC16 kernel procedure](../development/kernel.md).
Keep a known working kernel, its matching RAM root and UART recovery available.
Enable `Support SPCR` in firmware. On this sample it can be lost after power removal.
Use the [CIX SPCR loader correction](../boot/cix-spcr.md); UART1 and UART3 remain separate device interfaces.

The diagnostic trial selected a separate gzip-compressed kernel from the loader prompt:

```text
acpi cix-spcr
initrd root.fs
boot netbsd.vpu.gz -v
```

The gzip kernel SHA256 was
`3dd2062c2d43d4663147f67f90fba135fddfb59a3914f4aedf4b9cb8f657aa0e`.
The uncompressed ELF SHA256 was
`5afb9ee12a359d9007f03d8746d699bdaa34d61fba359c861b137ab1cf3924d2`.
The previously selected kernel and default boot entry were preserved.
These filenames describe the checked diagnostic media, not a published release layout.
Do not write arbitrary storage devices or reuse another board's firmware.

The VPU resource contract passes 50 assertions on the physical kernel.
Source contracts, GCC path analysis, host sanitizers and Linux CI memory checks
do not qualify firmware execution, live DMA or a usable codec.
Earlier firmware DDR training/BIST messages still require separate investigation.

For reusable board and interface references, start with
[DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).
Apply the [board contribution procedure](adding-a-board.md) to each new support claim.
