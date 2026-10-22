# CIX UART1/3 native rate consumer

`EMBER64_CIXUART_NATIVE` is an explicit manual profile. It includes the
read-only UART rate profile, keeps the VPU inactive, and retains the UART2
console gate. It enables the closed UART1/3 consumer with `plcom` flags 1.
The default profile and generic non-CIX, BCM and RP1 attachments keep their
existing paths. DEBUG UART2 (ACPI UID 3) bypasses the new rate consumer. CIXCOM0 remains
outside the closed UART1/3 profile; it is not a generic fallback.

The provider publishes a cache only after both functional rate requests,
IDs 264 and 266, pass framing, firmware status, body length and ACK checks in
this boot. The cache getter does no AML or device I/O. The CIX consumer waits
for the native interrupt-configuration phase, validates the cached ACPI root,
UID, exact resources and static self-referencing CLKT entries, then uses a
positive rate that fits `sc_frequency`. It does not assume 100 MHz or read
`_DSD` UCLK as Hz. A missing or unusable cache stops this attachment visibly.
It does not fall back to AML or firmware divisors after CIX qualification.
The native resource parser retains raw ACPICA interrupt triggering; the profile
requires level-sensitive interrupts. That parsed record does not retain
polarity, so this check does not independently qualify interrupt polarity.

The common PL011 divisor function uses bounded integer arithmetic. It rejects
invalid speeds and out-of-range divisors and carries fractional rounding into
the integer part. It enforces the range in
[Arm DDI0183G, section 3.3.6](https://documentation-service.arm.com/static/5e8e36c2fd977155116a90b5),
including zero fractional bits when the integer divisor is 65535. Zero frequency
with positive speed preserves the existing firmware-divisor path. Rate-dependent setup does not change clock gates,
resets or pinmux. It does not select a GNSS/IMU baud rate or enable UART DMA.

The actual-source contracts cover both provider count 0 and 1, deferred attach,
metadata/rate faults, console and generic-path preservation, and native divisor
vectors for 230400, 115200 and 9600 baud. Run `ember/tools/cix-plcom-contract.sh`
and `ember/tools/cixscmi-contract.sh`. Set `CIX_PLCOM_ANALYZE=1` and
`CIXSCMI_ANALYZE=1` for configured static analysis; the platform workflow also
runs sanitizers and Memcheck. Source checks do not prove physical serial traffic.

## Physical configuration boundary


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).
Same-open termios setup is not a wire-baud measurement or instrument exchange. External pinmux, electrical connections and interrupt delivery remain unqualified.
Use the native boot log to identify plcom units; COM names and ACPI UID are not device-unit aliases. The selected root needs matching MAKEDEV nodes.

## Combined manual peripherals profile

[EMBER64_CIX_PERIPHERALS](../../conf/EMBER64_CIX_PERIPHERALS)
includes `EMBER64_CIXUART_NATIVE` and adds the same fixed GPI5 input attachment
as [the separate GPIO profile](../../../../dev/acpi/docs/cix-gpio.md#manual-fixed-gpi5-inputs). It retains
the UART2 console gate, checked SCMI256 rate cache and GPI4 power button; the
VPU stays inactive. The source-tree default and both individual profiles are
unchanged. Generated configuration checks cover the combined attachment flags
and provider counts.

On one physical sample, source `24b6895ec` is installed as the persistent
default. One automatic warm boot completes the default countdown without a
menu selection. It attaches UART1/3 with checked 100 MHz rates and three GPI5
inputs, retains the UART2 console, and reaches clean FFS, authenticated SSH,
WPA2 and a Wi-Fi default route. Native `powerd`, `dhcpcd` and the SSH listener
are present. These checks do not prove a new DHCP exchange.

The old kernel remains an independent rollback copy. Recovery assets, the
manual trial kernel, boot configuration and EFI loader remain unchanged.
This acceptance covers the installed warm-default path. Cold boot with this
combined kernel, external UART traffic, GPIO transitions and PPS remain
unqualified. This warm boot does not retest button or wake behavior.
