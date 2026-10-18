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
including only clock ID67 for entity attributes and rate. That API exposes no
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

The source-only `scmi_smt_clock67_config_set(wire, enable, trace)` API sends
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
and serialized thread context belong to the caller. No ACPI binding, clock
backend or VPU consumer calls this helper. All discovery configurations,
including selector64, remain read-only. Physical gate control is unqualified.

The separate `cixscmi_acpi` diagnostic binds CIXHA006, UID 0. Its default
flags 0 mode performs no AML evaluation, claims, mapping or transport access.
The explicit
[EMBER64_CIXSCMI_QUERY](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_QUERY)
configuration selects flags 1 and disables deferred VPU provider queries.
VPU descriptor validation remains enabled. The normal
EMBER64 configuration does not select this diagnostic attachment.

The separate
[EMBER64_CIXSCMI_CLOCK_QUERY](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK_QUERY)
configuration selects exact flags 2. It sends only CLOCK PROTOCOL_VERSION,
protocol 0x14, message 0, with no clock ID argument. Flags 3 and all values
except 0, 1, 2, 4, 8, 16, 32 and 64 are rejected before deferred evaluation, mapping or bus access.
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
[EMBER64_CIXSCMI_CLOCK_ATTRS](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK_ATTRS)
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
[EMBER64_CIXSCMI_CLOCK67_ATTRS](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK67_ATTRS)
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
[EMBER64_CIXSCMI_CLOCK67_RATE](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_CLOCK67_RATE)
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
[EMBER64_CIXSCMI_REPEAT_QUERY](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_REPEAT_QUERY)
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
[EMBER64_CIXSCMI_MIXED_QUERY](../../sys/arch/evbarm/conf/EMBER64_CIXSCMI_MIXED_QUERY)
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
The [platform workflow](../../.github/workflows/cix-platform.yml) adds Linux
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

## Source checks and limits

The [native PowerResource framework](acpi-power.md) now preserves shared
ownership and failed transitions. It is not a complete D-state engine.
The VPU control helper validates the running CLKT, RSTL, _PR0 and _PR3
packages before acquiring these providers. The attachment defers the probe
until the ACPI children have attached. It copies attachment values and releases
parsed resource lists before deferring; it retains no stack arguments.

EMBER64 selects `flags 0x1`: validate metadata, acquire claims, query GCLK and
release untouched claims. `flags 0x0` retains descriptions-only attachment.
`flags 0x2` explicitly selects the active identity probe. Other flags are
rejected. No mode starts DMA, loads firmware or registers a video device.
The separate [EMBER64_CIXVPU_PROBE](../../sys/arch/evbarm/conf/EMBER64_CIXVPU_PROBE)
configuration selects mode 2. Build it with the common GCC16 toolchain.
Select its kernel manually and retain the working default and recovery paths.
The callback reports primary and cleanup errors, ACPI statuses, identity
validity and released controls. It runs at most once per attachment.

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
No diagnostic category was suppressed. The [CI workflow](../../.github/workflows/cix-platform.yml)
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

On 2026-10-17, GCC16.2 kernel `68d1fc3b020242c9967569816e5e0d35b3a3c771`
booted on Orange Pi 6 Plus, CIX CD8180, A4 PHECDA-Rev A, with firmware
`BiosV1.5-W20260706-e321056a`. CCLK and RST0 registered. The VPU stayed disabled.
The root filesystem was clean, and the installed power daemon restarted.

The separate `8230d5c73e319afe5c649a838543dd560316a6b2` query kernel validated
the running resource tables and acquired all three provider claims. GCLK for
ID 67 returned status `0xfffffffc`, SCMI NOT_FOUND. The query reported EIO,
released all claims and continued boot with a clean filesystem. The rate
was not accepted. This is a negative clock-query result, not codec readiness.
Neither trial activated the VPU, operated its reset or started DMA or firmware.

The firmware [clock IDs](https://github.com/cixtech/edk2-platforms/blob/1a48c6523a3225f3ef01b1c91eb3e3dc0dd1857f/Silicon/CIX/Sky1/Include/Protocol/ClockId.h)
name 67 as VPU_APBCLK and 271 as VPU_CLK. The actual CLKT names only 67.
Do not substitute 271 or invent a rate. Rate telemetry and successful gate
control require separate qualification. Both trials used a manual kernel
entry and preserved the working default, rollback and RAM recovery paths.

On 2026-10-18, diagnostic kernel `30b63316d07a6957aa7c5fff4e9cda6afbc4ac57`
passes one native SCMI BASE-version transaction on the same board and BIOS.
The GCC16.2 build uses `EMBER64_CIXSCMI_QUERY`; SCMI flags are 1 and VPU flags
are 0. Its gzip SHA256 is
`6bbfd4a98f6537215e7fc6d68bc6869ea4ad51ff3cac62a47f5c665103f644ef`.
The reply reports version `0x20000`, error 0, header `0x44000`, length 12 and
valid firmware status 0. The TX ACK snapshot is `0x8` before and after; no ACK
is cleared. Mappings and the channel lease remain until reboot.
The earlier pre-query rejection came from treating ACPICA's integer UID
capacity as an exact text length. The corrected binding passes 37227 source
checks and exact-head CI, including Memcheck and ASan/UBSan.
The boot reports clean FFS and retains the persistence marker. Wi-Fi completes
WPA2/DHCP recovery after an initial association interval; three gateway probes
pass. The default and rollback entries remain unchanged. This validates one
BASE exchange. It does not validate repeated requests, CLOCK version or clock
ID 67. VPU MMIO, IRQ, DMA and firmware execution remain disabled.

A separate manual boot of GCC16.2 kernel
`5b8fb039cd07287ffef931e2f8993687e309f766` sends one CLOCK-version request.
The gzip SHA256 is
`5de9d4eeb8205dc0d94b4c65838880970f5a42b550b205df0f6e7821a109c3cc`.
The reply contains `0x20000`, helper return 0, decode error 0, header `0x45000`,
length 12 and valid firmware status 0. The old exact-1.0 review policy prints
error 86; this is a local policy result, not a transport or firmware failure.
[DEN0056D section 4.6](https://documentation-service.arm.com/static/6231acfc8804d00769e9db69)
defines CLOCK 2.0. Its entity attributes add a clock-enable delay after the
16-byte name. Do not apply the older 20-byte entity schema to this reply version.
The boot reports clean FFS and restores WPA2/DHCP; three gateway probes pass.
No ACK is cleared, and the lease remains until reboot. Protocol attributes,
clock identity, rate and control remain unqualified. VPU MMIO, IRQ, DMA and
firmware execution remain disabled.

A further manual boot of kernel `f17c2aecdd9efc96143666578eed6846d9763005`
sends one CLOCK PROTOCOL_ATTRIBUTES request. The gzip SHA256 is
`9960ab00ea96f6e24b2a8e3203daaf60087ed2a21f079048f8c1940b4e0b975e`.
The reply reports raw `0x80111`, valid attributes, 273 clocks, eight asynchronous
slots, helper return 0, decode error 0 and firmware status 0. Header `0x45001`
and length 12 match the request. ID67 is in range; this does not establish its
identity or access permission. The selected schema is CLOCK 2.0; this boot
does not query version again. FFS is clean, and WPA2/DHCP plus three gateway
probes pass. VPU activation, per-clock attributes, rate and control remain
unqualified. No ACK is cleared, and the boot-long lease remains held.

A manual boot of GCC16.2 kernel `9b684797f63bd2fa399ebf02d2ffc1f7db682158`
sends one CLOCK_ATTRIBUTES request for ID67. The gzip SHA256 is
`9ff43cac32314667e3a7e3c4ba93f14dcedd607365265e7b2549917e654473b5`.
The firmware returns status 0, matching header `0x45003` and SMT length 28.
That length contains only 20 data bytes. The selected CLOCK 2.0 schema requires
24 data bytes, including enable delay. The binding reports decode error 96,
`raw_valid=0` and `entity_valid=0`. It does not decode a name, enabled state
or delay from that incomplete reply. The separately advertised version and this
reply length disagree; no compatibility policy or firmware cause is established.
Exact-head CI, including Memcheck and ASan/UBSan, passes. FFS is clean after boot.
The default and rollback entries remain unchanged. No ACK is cleared, and the
boot-long lease remains held. VPU activation, clock identity, rate and control
remain unqualified.

Two separate manual boots of GCC16.2 kernel source
`96d41f7f1013270869d504d6f212be9a15aab177` qualify further read-only evidence.
The rate variant has gzip SHA256
`0d26245ea5b57aa885aa726293bf8cb705d1054b1d03cd090b0d80dc36032595`.
Its one CLOCK_RATE_GET reply has header `0x45006`, length 16, firmware status 0,
helper return 0 and decode error 0. It reports `rate_valid=1` and `rate_hz=0`.
This is firmware telemetry, not a measured waveform or a proven reason for zero.
The entity variant has gzip SHA256
`70d370939c09b52cc0cb0478e21b40572401ada7a68bca8c0bd6b20b3ce62f00`.
It again receives only 20 entity data bytes. Its rejected-prefix record contains
attribute word 0 and name words `5f757076,63627061,00006b6c,00000000`.
Those name bytes spell `vpu_apbclk`, consistent with the vendor ID table.
The full CLOCK 2.0 entity record still reports EPROTO96 and invalid outputs;
enable delay is absent. This does not establish a compatibility policy or
clock-control permission. Both boots report clean FFS and retain their channel
leases without clearing ACK. Exact-head CI passes Memcheck and ASan/UBSan.
Each of those two boots issues only one request.

A manual boot of GCC16.2 kernel `7777a2bc7e9053950d5b2960a77aa3a8c92a0f2a`
qualifies three consecutive BASE_VERSION requests on one initialized wire and
one lease. The gzip SHA256 is
`1ba197821ac177c6c7e3d49f95b290be7c6c0fcd7160b04babbc7a5b861afe16`.
Headers `0x44000`, `0x84000` and `0xc4000` match their replies. Each response
reports version 2.0, length 12, firmware status 0, helper return 0 and decode
error 0. TX status remains `0x8` before and after each query. No ACK is written.
The summary reports three attempted requests and `qualified=1`. Exact-head CI,
including Memcheck and ASan/UBSan, passes. FFS is clean. After initial loss,
WPA2/DHCP recovers and 20 gateway probes pass on native channel149.
This bounded cold-boot result does not qualify an exported runtime service,
concurrent access, clock control, VPU MMIO, IRQ, DMA or firmware execution.

## Physical mixed-batch receipt

A manual boot of GCC16.2 kernel `44d9c0571458615dff5e5709adefca1eb4e38c1e`
passes all five read-only requests on one initialized wire and lease. The gzip
SHA256 is `e7a5016d43b89877bb4acd58c79ab05f6872b0372a7de0605a3c0ae5bf96ca40`.
BASE and CLOCK report version 2.0. Protocol attributes report 273 clocks and
eight asynchronous slots. Message7 attributes return zero. RATE_GET67 returns
zero Hertz as firmware telemetry. Tokens1 through5 match headers `0x44000`,
`0x85000`, `0xc5001`, `0x105002` and `0x145006`; each record has helper, decode,
policy, ACK and firmware status zero. TX status remains `0x8` around every query.
The summary reports five attempts and `qualified=1`. No ACK or setter is written.
Both exact-head CIX workflows pass, including Memcheck and ASan/UBSan. FFS is
clean and the root marker persists. WPA2/CCMP, DHCP and five gateway probes pass
on native channel149. Message7 availability does not prove permission to control
clock67. Runtime concurrency, gate control, waveform, VPU access, DMA and codec
firmware execution remain unqualified. The short ENTITY2.0 reply stays rejected.

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
