# CIX ACPI clock and reset providers

These native providers support the first bounded CIX P1 VPU resource path.
They are selected by EMBER64. Registration does not activate the codec.
The [VPU attachment](cix-vpu.md) has no operational DMA or firmware path.
Its default query does not activate the block or prove its physical power state.

## Clock provider

`cixclk_acpi` matches CIXHA010 with UID 0. It registers without calling GCLK
or CLKC. Claims are exclusive per clock ID. The API uses the native
CCLK AML methods rather than a second mailbox implementation.

| ID | Driver API operations | Intended consumer |
| --- | --- | --- |
| 0x43 / 67 | Rate query, enable, disable | VPU APB gate named by CLKT |
| 0x108 / 264 | Rate query only | UART1 functional clock |
| 0x10a / 266 | Rate query only | UART3 functional clock |

Different IDs have independent claims. Every request carries its actual ID.
UART clock claims reject enable and disable without evaluating AML. No PL011
consumer is added at this stage. This table limits the driver API; firmware
can still reject an operation. It does not establish physical UART frequency.

GCLK returns a little-endian status and a 64-bit rate. CLKC returns a
little-endian status. The implementation checks the returned object, payload
bounds, minimum length and complete status word before reading a value.
Failures preserve the output. Diagnostics identify the method and exception
or firmware status, including timeout statuses 6 and 11, without dumping data.
No operation changes the clock rate. A query still executes AML and is not
passive namespace inspection.

Enable or disable failure can follow a hardware change. The claim stays
uncertain until disable succeeds. An uncertain claim cannot be released or
enabled again. Repeated confirmed enable or disable returns EALREADY without
calling AML. Callers must use thread context and serialize claim lifetime.

## Native SCMI discovery foundation

`sys/dev/ic/scmi_smt_diag.[ch]` provides a separate checked discovery wire
helper. It is not a second clock provider or a fallback after AML failure.
The `scmi_smt_diag_query` API exposes a closed subset of BASE and CLOCK queries,
including only clock ID67 for entity attributes and IDs67/264/266 for rate.
That API exposes no
control or rate setter. Fixed clock67 control uses a separately named API below.

The caller must own validated CIX TX resources, ordering callbacks, a boot-long
exclusive channel lease and serialized thread context. The helper performs
aligned 32-bit SMT transactions and rings the CIX doorbell at offset 0x80.
It preserves implementation-defined SMT words. Replies must match the complete
request header, including protocol, message, type, token and reserved bits.
Length, minimum body size and protocol-list count are checked before output.
Nonzero firmware status preserves its full signed 32-bit value as a failure.

Each wait has a 30 ms deadline and a 3000-poll ceiling. Initial channel faults
cause no writes. Corrupt replies, completion failures and clock regression
poison the instance. Reinitialization and later callbacks are then forbidden.
Tokens are never reused; the engine stops after 1023 issued requests.
Replies remain unchanged on error; a separate trace reports failure evidence.

The actual-source wire contract covers the closed queries and fixed clock67
control with GCC16 path analysis and ASan/UBSan.
Fourteen causal mutations fail, including missing barriers and poisoned reuse.
The source cross-compiles with actual AArch64 kernel flags and GCC path analysis.
Target Cppcheck reports only unused public APIs and inherited header style;
CSA reports no findings. Host Cppcheck cannot parse the macOS SDK's `errno_t`
typedef in version 2.22.0; that failed run is retained separately.
These source checks do not establish firmware exchange.

The `scmi_smt_clock67_config_set(wire, enable, trace)` API sends
CLOCK_CONFIG_SET for fixed ID67. It accepts only the integer values0 and1.
It shares the query engine, token counter, ordering and bounded waits. There is
no arbitrary ID, message or payload input, and the read-only enum is unchanged.
The request is protocol0x14/message7 with ID67 then attributes0 or1. All other
attribute bits are zero. A successful response has status0 and no data body
(SMT length8), per [DEN0056D section4.6.2.8](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69).
The helper does not read an unexpected data body. If framing completes but a
successful response contains extra data, it returns EPROTO with trace COMPLETE.
The wire remains usable for an explicit disable attempt. A framed firmware
error returns EIO with its full signed status and also permits a later request.
Timeout, header, channel and clock faults poison the shared wire and prohibit
later I/O, including disable. Invalid input, state or token exhaustion performs
no callbacks and preserves trace. There is no automatic rollback or retry.
A control error can follow a hardware change; the caller must retain ownership
until it confirms a safe state. Capability qualification, exclusive clock claims
and serialized thread context belong to the caller. Only the explicit
selector 128 service below can call this helper through the clock backend.
All discovery selectors through 64 remain read-only.
The physical receipt below qualifies one native enable/identity/disable sequence.

### Explicit native clock67 service

Exact SCMI selector 128 runs the same five read-only readiness requests as 64.
It publishes a boot-long service only after every decode, policy and ACK check
succeeds. Setup remains cold-only. Published operations require thread context.
The service validates actual SCMI and logical CCLK handles in the same ACPI root.
It checks bounded cached CCLK HID/UID metadata and current resource ownership.
It does not activate CLKS or evaluate a replacement clock method.

`cixclk_acpi_get_native(logical, scmi, 67, out)` explicitly selects this backend.
It shares the existing clock67 claim with AML and pins the backend until put.
Native calls never fall back to AML. The ordinary kernel still omits SCMI;
its native acquisition returns ENOTSUP. Generated selection flags keep that
kernel free of unresolved service references. UART clock IDs remain AML-only.

Five readiness requests leave 1018 conservative helper calls. Enable requires
at least two calls. While enabled or uncertain, rate reads cannot spend the
last call reserved for disable. A failed final disable can exhaust the service;
there is no token reuse, reset or unlimited cleanup retry. A failed attempted
control retains an uncertain claim. Rejection before the helper preserves its
previous state. Framed firmware or semantic errors permit explicit disable.
Transport poison or an ownership/ACK fault prohibits further service I/O.
Reports keep helper, framing, firmware, ACK, budget and cleanup results separate.

The separate `cixscmi_acpi` diagnostic binds CIXHA006, UID 0. Its default
flags 0 mode performs no AML evaluation, claims, mapping or transport access.
The explicit
[EMBER64_CIXSCMI_QUERY](../../../arch/evbarm/conf/EMBER64_CIXSCMI_QUERY)
configuration selects flags 1 and disables deferred VPU provider queries.
VPU descriptor validation remains enabled. The normal
EMBER64 configuration does not select this diagnostic attachment.

The separate
[EMBER64_CIXSCMI_CLOCK_QUERY](../../../arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK_QUERY)
configuration selects exact flags 2. It sends only CLOCK PROTOCOL_VERSION,
protocol 0x14, message 0, with no clock ID argument. Flags 3 and all values
except 0, 1, 2, 4, 8, 16, 32, 64 and 128 are rejected before deferred evaluation, mapping or bus access.
This diagnostic does not query clock attributes, rates or clock ID 67.
The original BASE configuration and the normal EMBER64 configuration are unchanged.

CLOCK output separates the helper return, firmware status and binding decode.
`helper_return` is the helper's return value. A framed firmware error returns EIO
with valid full status; EIO alone does not identify a transport failure.
`decode_error` is EPROTO when a successful reply has more than the exact four-byte
version body. A short body fails inside the helper. No version word becomes valid
on either failure. Every decoded word, including zero, remains visible as raw data.
`reviewed_version` accepts exact 0x10000 and 0x20000. Other decoded values report local
`version_policy_error=ENOTSUP`. This policy does not claim SCMI incompatibility;
[DEN0056C section 4.1.3](https://documentation-service.arm.com/static/5fce1789e167456a35b368a7)
defines same-major minor revisions as compatible. Decode and policy fields are
not evaluated when the helper fails; check version and status validity first.
No result starts another request or changes a clock. The physical receipt below
qualifies one CLOCK_VERSION exchange; other clock commands need separate gates.

The separate
[EMBER64_CIXSCMI_CLOCK_ATTRS](../../../arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK_ATTRS)
configuration selects exact flags 4. It sends only CLOCK PROTOCOL_ATTRIBUTES,
protocol 0x14, message 1, without a clock ID. The VPU remains inactive.
The binding requires exactly four successful data bytes. It reports the raw word
with separate validity, then rejects reserved bits 31:24 before typed output.
The low 16 bits give `num_clocks`; bits 23:16 give `max_async`.
Zero values are valid. `id67_in_range` means only `num_clocks > 67`.
It does not establish clock visibility, rate telemetry or control permission.
Typed fields remain unchanged on failure; interpret them only when attributes
are valid. The selected Clock 2.0 schema comes from
[DEN0056D section 4.6.2.2](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69).
The output states that protocol version was not queried in this boot.
No result starts another request. The physical receipt below qualifies one
protocol-attributes exchange. Entity attributes use a separate selector and boot;
notifications and extended-name queries remain unavailable.

The separate
[EMBER64_CIXSCMI_CLOCK67_ATTRS](../../../arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK67_ATTRS)
configuration selects exact flags 8. It sends only CLOCK_ATTRIBUTES for ID 67,
protocol 0x14, message 3. The request contains literal ID 67; no returned name
selects another clock. Success requires the exact Clock 2.0 body of 24 bytes.
The binding records raw attributes, four little-endian name words and the full
32-bit enable-delay value before semantic validation. It then rejects reserved
bits 28:1, a missing NUL within 16 name bytes or a non-ASCII name prefix.
Empty names are permitted; bytes after the first NUL need not be zero.
The name is printed only as four hexadecimal words, including on semantic failure.
Firmware text is never passed to an unterminated string format.
Bits 31, 30 and 29 describe notification and extended-name capabilities;
bit 0 reports enabled state. None starts a notification or extended-name query.
The raw delay is in microseconds. Zero means unsupported delay telemetry,
not a measured zero latency. Interpret enabled, extended-name and delay-support
fields only with `entity_valid`. `raw_valid` is separate failure evidence.
The output states that version and protocol attributes were not queried in this
boot. The selected schema is 0x20000; see
[DEN0056D section 4.6.2.4](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69).
No result enables a clock, queries a rate or changes VPU resources. The physical
receipt below records a short firmware reply; entity attributes remain unqualified.

A checked successful entity response with only 20 data bytes gets a separate
rejected-prefix record. It prints only attributes and four name words as HEX.
The exact 24-byte guard still reports EPROTO, with full raw and entity validity
false. The record states that delay is absent; it does not synthesize a delay,
accept the old schema or change ownership. This uses only the checked local
reply. It adds no firmware request or mailbox access.

The separate
[EMBER64_CIXSCMI_CLOCK67_RATE](../../../arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK67_RATE)
configuration selects exact flags 16. It sends only CLOCK_RATE_GET for ID 67,
protocol 0x14, message 6. A successful body must contain exactly eight bytes.
Two LE32 words form the full unsigned 64-bit rate in Hertz, low word first.
Zero and UINT64_MAX are valid numeric replies; no guessed or fallback rate is
substituted. The decimal log uses the native 64-bit format and includes both
raw words. A failed helper or size check leaves the rate unchanged and invalid.
Interpret the rate only with `rate_valid`, and retain the separate helper return,
decode error and full firmware status. The output states that this boot did not
query version, protocol attributes or entity attributes. A rate can already be
stale after an asynchronous change; it grants no control permission.
See [DEN0056D section 4.6.2.7](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69).
No result sets a rate, enables a clock, clears ACK or starts another query.
The physical receipt below qualifies one rate read, without clock control.

The separate
[EMBER64_CIXSCMI_REPEAT_QUERY](../../../arch/evbarm/conf/EMBER64_CIXSCMI_REPEAT_QUERY)
configuration selects exact flags 32. This experimental manual diagnostic sends
three serial BASE_VERSION requests on one initialized wire and one owned lease.
The expected request headers are `0x44000`, `0x84000` and `0xc4000`; tokens are
not reset or reused. Each record retains the helper result, exact-body decode,
raw version, firmware trace and read-only TX status snapshot. Batch qualification
requires three complete four-byte replies with version `0x20000` and no pending
TX ACK bit1. This version prerequisite applies only to this batch.
A fresh read-only ACK snapshot precedes queries2 and3. Each blocked record
marks `query_called=0`; `attempted` counts actual helper calls. Logs are emitted
after the batch ends. An initial pending ACK rejects it before any request. Any failure or
pending ACK after a response stops the batch. It never clears ACK, retries,
reinitializes the channel or starts a fourth request. It retains the lease after
a possible publication. Each FREE/completion wait keeps the existing 30ms and
3000-poll ceilings, including a frozen-clock ceiling. Three queries permit at
most six waits. Earlier selectors still issue exactly one request; flags0 stays
passive. No result changes a clock or VPU resource. The physical receipt below
qualifies only this fixed three-request cold-boot sequence.

The separate
[EMBER64_CIXSCMI_MIXED_QUERY](../../../arch/evbarm/conf/EMBER64_CIXSCMI_MIXED_QUERY)
configuration selects exact flags 64 and keeps the VPU at flags0. One initialized
wire and lease carry five read-only requests: BASE_VERSION, CLOCK_VERSION,
CLOCK_PROTOCOL_ATTRIBUTES, CLOCK_PROTOCOL_MESSAGE_ATTRIBUTES for message7, and
CLOCK_RATE_GET for ID67. Tokens1 through5 produce headers `0x44000`, `0x85000`,
`0xc5001`, `0x105002` and `0x145006`. This fixed batch accepts exact BASE2.0
and CLOCK2.0. Unknown versions remain valid raw output; exact2.0 is a local
review policy, not a firmware-unsupported claim.
The protocol attribute word must have bits31:24 clear and count greater than67.
Message7 attributes must be exactly four data bytes with value0, as specified
by [DEN0056D section4.6.2.3](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69).
RATE_GET must have exactly eight data bytes; zero is valid telemetry.
Each record separates helper, firmware, decode, policy and ACK results. A raw
word is published only after the exact body length passes. Typed attribute and
rate values have separate validity. Fresh read-only ACK snapshots bound each
request, and any failure stops the batch before another request. Logs are
emitted only after the sequence ends. Initial pending ACK rejects before send;
later failures retain the lease. Five requests permit at most ten bounded waits.
The existing ENTITY2.0 length24 check remains unchanged; this batch does not
query or reinterpret entity data. No clock control, setter, ACK-clear, service,
VPU access or physical mixed-batch acceptance follows from a source result.

## Manual UART rate observations

[EMBER64_CIXSCMI_UART_RATE_QUERY](../../../arch/evbarm/conf/EMBER64_CIXSCMI_UART_RATE_QUERY)
selects exact SCMI flags256, VPU0 and the checked UART2 console gate.
It queries only functional clock IDs264 and266 with fresh tokens1 and2.
It shares the checked metadata, exclusive lease, bounded waits and ACK checks.
An error stops the sequence. A possible publication retains the boot-long lease.
The profile publishes no native clock service and performs no UART setup.
Exactly eight reply data bytes form each uint64 rate. Zero is raw telemetry,
not an accepted UART input frequency. Clock67 control and entity queries keep
their fixed ID rules. Instrument exchange and actual baud require separate
board tests.
The optional [IO observation profile](../../../arch/evbarm/conf/EMBER64_CIX_IO_OBSERVE)
adds only the [nonclearing header-mux snapshot](cix-gpio.md#manual-header-observation)
to that profile. It preserves VPU0, UART2 and the GPI4 button policy.

One physical CD8180 trial with source `db0adb0378` reports 100000000 Hz for
both IDs264 and266. Both replies contain eight data bytes, status0, matching
request headers and successful ACK checks. The summary reports two attempts,
two records, qualification and error0. The combined profile keeps the VPU
inactive and reaches the native UART2 root console. This accepts the fixed
rate observations only. It does not program PL011 divisors, enable a UART
clock or qualify serial data exchange. The measured rates are not defaults.

## Shared transport validation

Before mapping, the binding validates each static property, resource and
reference. It requires MBX6/7, SHM0/1, channel 8, non-coherent shared memory,
the reviewed interrupt profile and four unowned ACPI nodes. It reserves only
those nodes. Enabled transport interrupts cause rejection without writes.
ACPICA reports an integer UID's allocation capacity in its declared length.
The binding checks that complete span, then matches the expected UID including
its NUL terminator. It does not require unused capacity bytes to be zero.
Pre-query rejection prints a constant validation stage, role and native error.
Those diagnostics add no firmware evaluation or transport access.
Mappings use Device memory and explicit completion barriers. One selected
discovery request or fixed batch runs in serialized cold-boot thread context.
No IRQ is installed or cleared. The binding exposes no retry, setter, clock service or detach path.
Once a request can be published, mappings and claims remain until reboot,
including on timeout or malformed reply. Before that point, failures release
only resources that the diagnostic still owns.

The actual-source binding contract covers compact string UIDs and native
integer-UID capacity, full-span bounds, exact NUL matching and failed-stage
diagnostics. GCC16 path analysis, Cppcheck, CSA and ASan/UBSan check the same source.
Eighteen original causal mutations fail.
Native target compilation passes. Target analysis retains inherited header
findings and fixed callback-signature style hints. No category is suppressed.
These checks do not qualify a complete kernel boot or physical transaction.
The [platform workflow](../../../../.github/workflows/cix-platform.yml) adds Linux
Memcheck. Check its exact-head result before interpreting a CI claim.

Deferred attachment runs before `initclocks`. Uptime can remain frozen there;
the independent 3000-poll ceiling still bounds the wait. The nominal 30 ms
budget excludes software and MMIO overhead. Use a manual boot entry and keep
the working default and recovery kernels. The printed version-valid and
status-valid fields distinguish accepted output from failure evidence.

Masked CIX ACK behavior across repeated requests is unqualified. Linux's polled
SMT completion alone does not prove that an ACK interrupt can stay disabled
and uncleared. Do not add an ACK sequence or change disabled ACPI clock children
from this wire helper's success. A single BASE-version query is a transport
discriminator; it does not establish clock ID 67 or enable VPU resources.

Run the source contract with GCC16:

```sh
CC=gcc-16 CFLAGS='-O2 -g -fanalyzer' sh ember/tools/scmi-smt-diag-contract.sh
CC=gcc-16 CFLAGS='-O2 -g -fanalyzer' CIXSCMI_ANALYZE=1 \
    sh ember/tools/cixscmi-contract.sh
```

## Reset provider

`cixreset_acpi` accepts one exact RST0 profile: CIXHA020, UID 0, non-coherent,
one read/write fixed memory resource at 0x16000000 with length 0x1000. It
rejects extra or unsupported resource descriptors before mapping. Attachment
maps and registers only; it does not read or change reset registers.

Only two reset IDs are supported:

| ID | Offset and mask | Meaning |
| --- | --- | --- |
| 0x0e | 0x400, bit 11 | VPU reset, active low |
| 0x8e | 0x804, bit 19 | VPU RCSU reset, active low |

An exclusive claim does not access MMIO. Assert clears the owned bit;
deassert sets it. A sleep mutex covers the complete read, update, barrier
and readback. Other bits are preserved. Readback failure retains an uncertain
claim. Only a confirmed assert recovers it. Put accepts untouched or confirmed
asserted claims. A state query reads one owned bit without writes or changes
to ownership state; it cannot recover an uncertain claim.

The consumer owns power sequencing and reset timing. These providers do not
choose a VPU RCSU reset sequence, expose arbitrary register access, toggle
other reset IDs or implement general CIX clock and reset support. They are
built in with boot-long lifetime. Hot detach and suspend/resume are not supported.

## ARM ACPI DMA address limits

The ARM ACPI DMA tag path resolves an IORT named component with the device's
ACPI namespace handle. Passing the `acpi_devnode` pointer instead lost the
component's `MemoryAddressLimit` and selected the unrestricted fallback.
The fix preserves the existing range-length, `_DMA` translation and `_CCA`
policies. A child tag and loaded-address checks remain separate requirements.

Run the actual-source regression with the common GCC16 host compiler:

```sh
CC=gcc CFLAGS='-O2 -g -fanalyzer' IORT_DMA_ANALYZE=1 \
  sh ember/tools/acpi-iort-dma-contract.sh
```

The runner executes five complete native functions extracted without algorithm
changes. It uses the actual ACPICA table types and modeled namespace, resource
and allocation services. All 113 checks pass. They cover 40/32-bit limits,
missing/zero limits, `_DMA` translations, coherency flags and cleanup.
The complete native source also compiles with GCC16 and path analysis.
These checks do not validate arbitrary IORT input, establish an SMMU domain,
or qualify the physical VPU stream route.

## Source checks and limits

The [native PowerResource framework](acpi-power.md) now preserves shared
ownership and failed transitions. It is not a complete D-state engine.
The VPU control helper validates the running CLKT, RSTL, _PR0 and _PR3
packages before acquiring these providers. The attachment defers the probe
until the ACPI children have attached. It copies attachment values and releases
parsed resource lists before deferring; it retains no stack arguments.

EMBER64 selects `flags 0x1`: validate metadata, acquire claims, query GCLK and
release untouched claims. `flags 0x0` retains descriptions-only attachment.
`flags 0x2` explicitly selects the AML identity probe. Exact `flags 0x3`
selects the native clock backend. Exact `flags 0x4` selects the
[CPU-only allocation fixture](cix-vpu.md#manual-cpu-only-allocation-probe)
after root mounts; it does not call this resource-control helper or select SCMI.
Exact `flags 0x5` selects the [CPU preparation fixture](cix-vpu.md#manual-cpu-preparation-probe)
with the same control restrictions.
Exact `flags 0x6` selects the [native hardware profile](#manual-native-hardware-profile).
Other flags are rejected. No mode starts device DMA, executes firmware or
registers a video device.
The separate [EMBER64_CIXVPU_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_PROBE)
configuration selects mode 2. Build it with the common GCC16 toolchain.
Select its kernel manually and retain the working default and recovery paths.
The callback reports primary and cleanup errors, ACPI statuses, identity
validity and released controls. It runs at most once per attachment.

## Manual native hardware profile

[EMBER64_CIXVPU_PROFILE_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_PROFILE_PROBE)
includes the native identity profile, preserving SCMI service128 and selecting
VPU mode6. Build it with GCC16 and select it manually. Preserve a working default
and recovery kernel. The profile requires boot context with `cold == 1`.
It accepts only MEM0 `0x14230000`, MEM1 `0x14240000`, two `0x10000` spans,
GSI358, `_CCA=0`, ID `0x56648002`, four cores, four slots and SVN `0xe0c1afe1`.
Other otherwise valid locations or revisions return ENOTSUP for this profile;
the general resource validator remains unrestricted.

The existing native power, clock67 and reset sequence reads identity first.
The same control context then maps MEM0 and reads six words at offsets
`0x308`, `0x300`, `0x21c`, `0x10`, `0x14` and `0x18`. It also reads MEM1+`0x24`.
These are raw STRAP_PIN2, STRAP_PIN0, PGCTRL, repair PASS/ENBL/BUSY and CLKFORCE.
The report derives the reference usable-core mask from inverted STRAP_PIN2
bits7:4. Sparse and zero masks remain valid observations.

All bounds checks precede the seven reads. READ barriers preserve their order;
the snapshot is not atomic. Both mappings are released before existing checked
reset/clock/power cleanup. A valid raw report can coexist with cleanup failure.
The active transaction includes the existing power/reset writes; the additional
snapshot adds no writes, repair requests or functional-clock271 operations.
No BUSCTRL or LSID read, DMA, firmware execution or interrupt setup occurs.
`bus_space_read_4` has no errno; external-abort recovery is not implemented.
Source contracts pass 5242 checks, host ASan/UBSan and six causal mutations.
Native GCC16 passes; native Cppcheck/CSA retain only their recorded baseline.
On the physical board described in the receipt below, GCC16 kernel
`3c7595278fcf5c602c5c6b1aa1778208bc2b8f46` reports a valid profile:

| Raw field | Observed value |
| --- | --- |
| STRAP_PIN2 | `0` |
| STRAP_PIN0 | `0x0c0c0c00` |
| PGCTRL | `0x07cefffc` |
| Repair PASS / ENBL / BUSY | `0 / 0 / 0` |
| CLKFORCE | `0x200` |
| Derived usable core mask | `0xf` |

Clock67 enable/disable, identity and checked cleanup return success.
The additional reads request no fresh repair. Electrical rails, clock waveform,
functional clock271, repair execution and DMA routing retain separate boundaries.

The separate
[EMBER64_CIXVPU_NATIVE_PROBE](../../../arch/evbarm/conf/EMBER64_CIXVPU_NATIVE_PROBE)
selects SCMI 128 and VPU 3. It requires one same-root cached SCMI HID/UID match
and a ready native service. Missing or ambiguous providers fail before power
or reset control. It keeps the same power, RCSU, main-reset and cleanup sequence.
An enable error prevents VPU mapping and identity reads. Failed native disable
retains reset, power and claims. This explicit profile does not start DMA,
load firmware or establish an interrupt handler.

The helper supports a strict rate query with no power or reset operation.
It records query-attempted and rate-valid separately; a decoded zero can be valid.
Its separate
active probe requires an already deasserted RCSU reset. It uses native power
ownership, confirms the power bit, requires successful clock enable, pulses the main reset
and reads identity and geometry. Cleanup asserts reset before releasing
clock and power. Failed cleanup retains its context and reports a separate
error. A power attempt is pending rollback, not proof of a native lease.
An OFF readback fault after lease release requires diagnosis, not OFF replay.
This probe has no DMA or interrupts. Its cleanup cannot serve a DMA driver.
Power-bit confirmation does not establish memory repair or firmware readiness.
The active probe does not query the rate or require rate telemetry. This follows
the maintained runtime-resume sequence. It reports the frequency as unmeasured;
it does not suppress a failed rate request or substitute another clock ID.

Run from the source root with GCC16:

```sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXCLK_ANALYZE=1 \
  sh ember/tools/cixclk-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXRESET_ANALYZE=1 \
  sh ember/tools/cixreset-contract.sh
CC=gcc-16 CFLAGS='-O1 -g -fsanitize=address,undefined' \
  sh ember/tools/cixclk-contract.sh
CC=gcc-16 CFLAGS='-O1 -g -fsanitize=address,undefined' \
  sh ember/tools/cixreset-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' \
  sh ember/tools/acpi-power-contract.sh
CC=gcc-16 CFLAGS='-O2 -fanalyzer' CIXVPU_CONTROL_ANALYZE=1 \
  sh ember/tools/cixvpu-control-contract.sh
```

The contracts compile all production functions with modeled OS services.
Clock checks exercise real response parsing, ownership, error recovery and
cause diagnostics. Reset fixtures use actual public ACPICA resource types;
AML byte decoding is outside that model. Two reset threads perform 8000
operations through a real host mutex. These are userspace source contracts,
not kernel execution or evidence of physical reset behavior.

GCC16 host contracts pass 65393 clock checks and 383 reset checks. Two clock
threads also execute 8000 rate transactions for the independent UART claims.
The source
also compiles with actual AArch64 kernel flags and GCC path analysis. Host
Cppcheck and Clang Static Analyzer cover the contracts. Target analysis
retains inherited header style findings and fixed callback-const suggestions;
the clock target CSA also reports four inherited bitops dead stores.
No diagnostic category was suppressed. The [CI workflow](../../../../.github/workflows/cix-platform.yml)
adds Linux ASan/UBSan and complete Memcheck runs. Check its exact-head result.
The power contract passes 87 assertions; the VPU control contract passes 447.
The latter compiles both the complete helper and actual attachment source.
The power module retains inherited CSA dead stores in unchanged functions.
Those findings are recorded separately; its execution contract does not
claim clean analysis of the whole ACPI power module.

A complete kernel build, provider attachment, real clock transactions,
reset readback and codec execution are separate acceptance boundaries.
Source checks alone do not establish physical provider behavior.

## Physical registration and query boundary


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).
The AML GCLK path and native SCMI path have different results. Do not substitute clock271 for the CLKT-specified clock67.
A zero RATE_GET value is firmware telemetry, not a measured waveform. The truncated CLOCK2.0 entity reply remains rejected.

## Physical mixed-batch receipt


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).
Successful read-only requests do not authorize setters or concurrent service access.
The BASE/CLOCK versions, token framing, status and body length must pass independently for each response.

## Physical native clock67 and VPU identity receipt


Current hardware evidence and its applicability belong to the [DevBoards profile](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/cix-p1-orangepi6plus/interfaces.md).
The finite clock67 enable/identity/disable sequence does not qualify functional clock271, electrical rails, waveform, latency or unlimited runtime PM.
Keep the transport lease and control ownership rules above even after a successful diagnostic.

## Primary provenance

Hardware facts come from the pinned vendor
[ACPI clock implementation](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/clk/cix/acpi_clk.c),
[reset register table](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/reset/reset-sky1.c)
and [reset IDs](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/include/dt-bindings/reset/sky1-reset.h).
The checked board firmware describes CCLK, RST0 and the VPU CLKT/RSTL references.
These original BSD providers do not include or relabel GPL implementation code.
The single-request diagnostic also uses the pinned
[SCMI ACPI description](https://github.com/radxa/edk2-platforms/blob/14cf7bdcfe1d0e8635a16a25d4209b6e28b18fca/Platform/CIX/Sky1/Drivers/AcpiSocTables/Dsdt-ScmiMailbox.asl)
and [mailbox register roles](https://github.com/radxa/kernel/blob/77736302973085a9d0ffb397ec4ee4ef795160f4/drivers/mailbox/cix-mailbox.c)
as hardware references. Its BSD source is original.
