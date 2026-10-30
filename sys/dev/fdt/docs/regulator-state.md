# Observing FDT regulator state

`fdtbus_regulator_is_enabled(reg, &enabled)` observes an acquired regulator
without enabling it, changing its voltage, or delaying for a ramp. The result
is updated only on success. A provider without the optional `is_enabled`
callback returns `EOPNOTSUPP`; provider errors are preserved. A null result
pointer returns `EINVAL`. Existing providers and enable/disable calls keep
their previous behavior.

AXP8191 and AXP PMIC regulators with an enable mask read the existing enable
register through the I2C bus acquire/read/release path. AXP8191 DCDC2 uses
register `0x10`, bit 1; DCDC4 uses register `0x10`, bit 3. AXP PMIC I/O
regulators compare the full masked enable value, which can contain multiple
bits. The query does not infer state from DT boot flags or from the programmed
voltage. A failed bus acquire or register read leaves the output unchanged and
does not trigger a write or retry inside the provider. A control without an
enable mask returns `EOPNOTSUPP`.

This is an observation, not an ownership claim or a reservation. Another
consumer can change the regulator after the read. The enabled bit and
`get_voltage` setting do not measure the physical output or establish that
a connected GPU/NPU is ready for MMIO. Power domains, clocks, reset state,
and consumer sequencing require separate checks. This change creates no
GPU/NPU consumer and does not establish hardware acceleration.

The existing AXP8191 register table agrees with Allwinner's regulator
implementation in [the pinned Orange Pi BSP](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/power/regulator/axp2101-regulator.c).
The query uses that existing table; no vendor implementation was copied.

Run the portable production-body contract from the source root with the host
compiler selected by the EmberBSD SDK:

```sh
CC=/absolute/path/to/selected-sdk/bin/clang sh ember/tools/regulator-state-contract.sh
CC=/absolute/path/to/selected-sdk/bin/clang \
    REGULATOR_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/regulator-state-contract.sh
```

It checks all 39 AXP8191 entries against every possible enable-register byte,
AXP PMIC DCDC4 and multi-bit I/O enable values, I2C acquire/read errors, output
preservation, and legacy enable/disable behavior. The contract does not access
hardware. Native board acceptance is separate.
