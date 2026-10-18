# BRCMSTB I2C core

This software foundation provides bounded combined reads with the BSD `ic_exec`
signature. It has no native adapter, bus attachment, IRQ handler or hardware
acceptance. It does not detect a connector or parse EDID.

## Interface and limits

The core is in `sys/dev/i2c/brcmstb_i2c.c`. The owner supplies ordered MMIO,
monotonic time, delay and interrupt-mask guard callbacks. It serializes all calls.
Initialize new storage once. Never reinitialize a retained owner.

The supported operation is exactly `I2C_OP_READ_WITH_STOP`:

- One command byte and 1 through 128 data bytes.
- A 7-bit address value and nonnull command and data pointers.
- Flags `0` or `I2C_F_POLL` and the fixed 97,500 Hz selector.
- Eight 4-byte data registers and at most 32 data bytes per hardware command.

The core rejects unsupported operations and invalid arguments before callbacks.
It does not support writes, quick operations, PEC, block mode, 10-bit addresses,
non-STOP operations or another speed. A native private tag must reject PEC before
calling generic `iic_exec`, which processes PEC before its controller callback.
Generic `iic_exec` also requires explicit bus acquisition and release.

A one-byte offset followed by 128 data bytes needs five hardware commands.
Their condition bits, without ENABLE, are `0x50`, `0x10`, `0x30`, `0x30`, `0x20`.
The offset uses address byte `addr << 1`. The read uses `(addr << 1) | 1`.
Further chunks continue the same read. Data registers use the low byte first.
Only a complete successful transaction changes the caller's output buffer.

## Ownership, deadlines and failure

The initial guard must prove that the inherited interrupt leaf is masked and has
no pending source for this bus. Before every write, the guard must prove that the
mask remains set. An owned completion can be pending after command start.
Polling still enables the peripheral `CTL.INT_EN` bit. It must not unmask its
parent interrupt leaf. The guard never writes registers.

Success disables peripheral INT_EN and clears COUNT and ENABLE after each
command. Bounded readback verifies the defined fields. Unknown register bits
are preserved or excluded from status comparisons. The owner must separately
check the parent pending state before releasing platform resources. Read-only
idle and completed STOP do not prove the electrical state of SCL or SDA.

All phases share a 100 ms elapsed-time deadline and a 10,000-sample polling
budget. A backwards clock or wrapping timestamp fails with `EIO`. Elapsed-time
subtraction avoids overflow when the initial timestamp is near its maximum.
The polling budget also bounds a frozen clock. Each unsuccessful sample requests
a 10 microsecond delay.

These limits bound software polling. They cannot interrupt a stalled callback,
a stalled MMIO access or scheduler latency. Native `bus_space` cannot recover a
synchronous abort or guarantee a hardware wall-clock return time.

Callbacks return zero or a positive BSD errno. A negative callback error becomes
`EIO`. A write attempt is recorded before the write callback. After any failure,
the core performs no further writes. A failure after a write attempt latches a
fault. Later transfers and finalization return that fault without hardware access.
The owner must retain callbacks, mappings, clocks and all related resources.
There are no retries, extra STOP commands, reset writes or GPIO recovery.

## Software checks

Run the actual production source with controlled MMIO and time services:

```sh
sh ember/tools/brcmstb-i2c-contract.sh "$PWD"
CC=clang CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    BRCMSTB_SKIP_MUTANTS=1 sh ember/tools/brcmstb-i2c-contract.sh "$PWD"
```

The harness uses the production BSD operation, flag and type headers.
It replaces kernel services only. It checks chunk boundaries, address bytes,
conditions, byte packing, partial failures and unchanged output. It injects an
error at every error-returning callback position. Separate cases check inherited
masks, pending state, NACK, deadline, frozen time, backwards time and wraparound.
Causal mutants must compile and then fail a behavioral assertion.

`CC`, `CFLAGS`, `BRCMSTB_COMPILE_ONLY` and `BRCMSTB_OUTPUT` support a cross-built
contract. `BRCMSTB_EVIDENCE` saves exact source inputs. `TEST_RUNNER` selects an
execution wrapper. `TMPDIR` selects temporary storage. Each run prints its exact
temporary path before compilation and removes only that path on exit.
These checks do not establish MMIO safety, electrical timing or physical DDC.

## Source facts

Register facts come from Raspberry Pi Linux commit
[`a1a601d4857978d810175365550c4536701df4ed`](https://github.com/raspberrypi/linux/tree/a1a601d4857978d810175365550c4536701df4ed),
[`i2c-brcmstb.c`](https://github.com/raspberrypi/linux/blob/a1a601d4857978d810175365550c4536701df4ed/drivers/i2c/busses/i2c-brcmstb.c).
Its SHA256 is `7b63e056d6f03ab99cbb4f7361f303f97fb13f3d895d270de83a530ab6885eb8`.
The implementation is BSD-2-Clause. It uses register facts and does not copy the
GPL implementation. This change was prepared with AI assistance.
