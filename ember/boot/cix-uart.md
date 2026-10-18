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

On one physical sample, source `10105af84` attaches UART1 as `plcom1` with
cached clock264 at 100000000 Hz and UART3 as `plcom3` with clock266 at the
same rate. Native `stty` on the same open dialout descriptor sets and reads
UART1 at 230400 8N1, and UART3 at 9600 and 115200 8N1. DEBUG UART2 remains
the console. These checks do not measure the wire baud or exchange instrument
data. External pinmux, electrical connections and interrupt delivery remain
unqualified. Do not infer a device unit from COM names or ACPI UID; use the
boot log. Reduced images need the corresponding native `MAKEDEV plcomN` nodes.

## Combined manual peripherals profile

[EMBER64_CIX_PERIPHERALS](../../sys/arch/evbarm/conf/EMBER64_CIX_PERIPHERALS)
includes `EMBER64_CIXUART_NATIVE` and adds the same fixed GPI5 input attachment
as [the separate GPIO profile](cix-gpio.md#manual-fixed-gpi5-inputs). It retains
the UART2 console gate, checked SCMI256 rate cache and GPI4 power button; the
VPU stays inactive. The default kernel and both individual profiles are
unchanged. Generated configuration checks cover the combined attachment flags
and provider counts. The combined kernel has not passed a physical boot.
