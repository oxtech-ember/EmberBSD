# Observing FDT regulator state

`fdtbus_regulator_is_enabled(reg, &enabled)` observes an acquired regulator
without enabling it, changing its voltage, or delaying for a ramp. The result
is updated only on success. A provider without the optional `is_enabled`
callback returns `EOPNOTSUPP`; provider errors are preserved. A null result
pointer returns `EINVAL`. Existing providers and enable/disable calls keep
their previous behavior.

AXP8191 reads the regulator's existing enable register and mask through the
I2C bus acquire/read/release path. DCDC2 uses register `0x10`, bit 1; DCDC4 uses
register `0x10`, bit 3. The query does not infer state from DT boot flags or
from the programmed voltage. A failed I2C transaction leaves the output
unchanged and does not trigger a write or retry inside the provider.

This is an observation, not an ownership claim or a reservation. Another
consumer can change the regulator after the read. The enabled bit and
`get_voltage` setting do not measure the physical output or establish that
a connected GPU/NPU is ready for MMIO. Power domains, clocks, reset state,
and consumer sequencing require separate checks. This change creates no
GPU/NPU consumer and does not establish hardware acceleration.

The existing AXP8191 register table agrees with Allwinner's regulator
implementation in [the pinned Orange Pi BSP](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/power/regulator/axp2101-regulator.c).
The query uses that existing table; no vendor implementation was copied.

Run the portable production-body contract from the source root:

```sh
sh ember/tools/regulator-state-contract.sh
REGULATOR_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/regulator-state-contract.sh
```

It checks all 39 AXP8191 entries against every possible enable-register byte,
I2C acquire/read errors, successful retries, output preservation even when a
provider clobbers its private error result, and legacy enable/disable behavior.
The contract does not access hardware. Native board acceptance is separate.
