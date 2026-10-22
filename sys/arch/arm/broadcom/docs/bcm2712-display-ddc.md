# BCM2712 native DDC0 owner

This explicit kernel API connects the generic BRCMSTB I2C core to the native
BCM2712 display resources. It has no boot attachment or public I2C tag.
It provides the unchanged 128-byte base read and a separate complete read of
128 or 256 bytes from address `0x50`. The complete API supports at most one extension. It does not
scan the bus, use an E-DDC segment pointer, enable interrupts, reset hardware,
change clocks or provide modesetting.

## Build and caller contract

`BCM2712_DISPLAY_DDC` selects the adapter only with `bcmv3d_acpi`.
Its dependencies are the `brcmstb_i2c`, `i2cexec` and `edid` attributes.
The ordinary configuration does not select this option. The experimental
`EMBERV3DRENDER` configuration selects the API. It does not start transfers
or perform automatic MMIO. Load a finite acceptance probe explicitly.
The core is `sys/dev/i2c/brcmstb_i2c.c`. The adapter and API are
`bcm2712_display_ddc_acpi.c` and `bcm2712_display_ddc.h` under
`sys/arch/arm/broadcom/`.

The lifecycle caller must exclusively own accessible mappings for resource13
BSC, `0x107d508380/0x10`, and resource14 DDC0, `0x107d508200/0x58`.
It keeps HSM and DVP leases, power and both mappings valid until destroy succeeds.
It excludes firmware and every other BSC or BSC interrupt consumer.
The inherited mask check is not an ownership proof. The
[read-only snapshot](bcm2712-display-dvp.md#finite-ddc0-snapshot) does not
establish SCL/SDA levels, a free bus or a general ACPI power lifetime.
Native bus_space cannot recover a stalled transaction or synchronous abort.

All API operations require sleepable process context. Hard and soft interrupt
calls fail before MMIO or mutex acquisition. `I2C_F_POLL` does not make this
owner IRQ-safe. The caller must quiesce users before releasing its last provider
reference and must not reuse a pointer after successful destroy.

## API and failure policy

1. `bcm2712_display_ddc_create` validates the full canonical display contract
   and the two exact mapped descriptors. It initializes the private I2C tag
   before setting its cookie and mandatory `ic_exec`. The cookie is the core;
   the core callback argument is the native provider. Output is unchanged on error.
2. `bcm2712_display_ddc_read_edid` requires a non-null output and length 128.
   It acquires the private bus, performs one combined polled I2C operation into
   scratch storage, checks the post-transfer BSC guard, then releases the bus.
   Release occurs only after successful acquire. The actual `edid_is_valid`
   checks the signature and checksum before output is copied.
3. `bcm2712_display_ddc_read_edid_complete` requires capacity at least 256 and
   separate valid writable buffer and length outputs. One provider lock and one
   POLL acquire cover both blocks. Offset 0 reads and validates the base signature,
   checksum and extension count. Count 0 returns length 128 with a zeroed second
   half. Count 1 reads offset 128 and checks its independent 128-byte checksum, then
   returns length 256. Count > 1 returns clean `EOPNOTSUPP`; no partial result or
   segment transaction follows. Success changes exactly the first 256 buffer
   bytes and the length. Larger-buffer tails and both outputs on error stay
   unchanged. Extension checksum validation does not parse or accept sink
   capabilities, HDMI mode, QS, AVI metadata or a timing mode.
4. `bcm2712_display_ddc_status` returns cached core state, native fault and
   validity-labelled BSC observations. It performs no MMIO. The BSC words are
   separate reads, not an atomic or current hardware-state guarantee.
5. `bcm2712_display_ddc_destroy` waits for the current serialized operation.
   Success removes the registry entry, finalizes the private tag and frees storage.
   A subsequent call through an unregistered pointer is rejected before dereference.

The global process-context mutex serializes both read APIs, status and
destruction. The existing base API keeps its original address/command/length,
acquire/exec/post-guard/release/validate/copy order and ABI.
The provider permits only one registered DDC0 owner. Its callbacks reside in
kernel code. A failed diagnostic module cannot leave module-text callbacks.

Before each core write, the native guard requires BSC mask status+4 bit1 set.
The initial and post-success guard also requires BSC status+0 bit1 clear.
It does not write the BSC mask or status and does not establish a GSI handler.
An unexpected guard state faults a registered owner. Any error returned by an
already invoked `iic_exec` also faults the native owner, even if the generic
core detected busy state before its first write. This stricter native policy
retains resources after an unknown controller state. The pure core keeps its
separate transport and mutation policy.

Invalid arguments and a failed bus acquire do not become transport faults.
A bad EDID after successful transport and post-guard returns `EBADMSG` with
fault0 and unchanged output. An unsupported extension count also stays clean.
These data/policy errors permit ordinary destroy. Each block has the existing
core deadline and poll budget; two blocks do not gain a hard native-MMIO bound.
Cached core status describes the last exec, not a cumulative two-block transfer.
Every other fault is sticky. Destroy returns the fault and preserves the
registry, tag and callback storage. The caller must retain clocks, leases and
mappings for separately controlled recovery. No automatic retry, clear, STOP,
clock change or bus recovery follows a failed transfer.

## Source validation

```sh
sh ember/tools/bcm2712-display-ddc-contract.sh "$PWD"
DDC_SKIP_MUTANTS=1 CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/bcm2712-display-ddc-contract.sh "$PWD"
```

The fixture compiles the actual decoder, generic core and native adapter.
It extracts complete unchanged BSD iic dispatch functions and the EDID validator,
including their source licenses. An instrumentation boundary records the exact
iic request, then calls the real core. Only kernel services and bus-space hardware
are controlled. Mutexes use host pthreads. These checks do not validate physical
DDC, the peripheral clock, IRQ operation or EDID on a connected display.

The fixture checks both APIs, independent block data/checksums, capacity and
unchanged-output boundaries, every command/write guard, both-block failures and
serialization while the second exec is blocked. Its base MMIO transcript matches
the accepted EmberBSD 6245a467 adapter with the same actual core/iic input. The
old base path and the complete path share the same native owner and callbacks.
Focused causal mutants target exact functions, compile before execution and use
the same TEST_RUNNER as the positive. Sanitizer and analyzer evidence remains
separate from a linked coherent kernel or physical transfer.

`DDC_EVIDENCE` saves the exact inputs, original paths and SHA256 of the bounded
iic/EDID extractions. `CC`, `CFLAGS`, `DDC_COMPILE_ONLY`, `DDC_OUTPUT` and
`TEST_RUNNER` support cross-built tests and supported memory checkers.
`DDC_SKIP_MUTANTS=1` runs only the positive contract. `DDC_MUTANTS` selects
focused causal cases. `CFLAGS=-DDDC_BASE_ONLY` limits the fixture to the old base
contract for a paired pre-change source comparison. Valgrind is unavailable
on the Darwin AArch64 host; ASan and UBSan are separate checks.

Register facts and prior-art limits are in the pinned
[generic core documentation](brcmstb-i2c.md). No GPL driver implementation
is copied into this adapter.

The new complete API needs a matching kernel build before a caller can use it.
It does not add a module-private adapter or registry. An existing retained PHY
owner does not permit a second HSM/DVP owner or implicit borrowing of its leases.
Do not copy this whole adapter into an unloadable module: its RUN_ONCE mutex and
registry need an explicit lifecycle. Physical extension transfer remains open.
