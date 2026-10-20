# A733 NPU diagnostic attachment and VIPLite subset

The Zero 3W device tree enables the existing NPU consumer. It keeps
`netbsd,consumer-managed-power` and `netbsd,observe-only` on the node.
Other A733 board trees keep the SoC default, `status = "disabled"`.
AI assistance was used for this change and its source controls.

## Attachment boundary

The NPU node has no pinctrl configuration. The generic FDT pre-attach path
returns before power-domain enable for a valid empty
`netbsd,consumer-managed-power` property. NPU attachment records the
register-window metadata, initializes its mutex, and registers sysctl and
`vipcore`. It does not map NPU registers, enable clocks, deassert resets,
request power, allocate DMA, or submit commands. The observe-only property
describes that attachment boundary. It does not block explicit runtime
stages or register-read ioctls.

Read `hw.sun60inpuid0.vipmajor` and the driver registration before creating
or opening a device node. An old `/dev/vipcore` on disk does not establish
that the current kernel registered that major number. Normal boot must
leave NPU stage 0 and preserve the accelerator clocks and power state.
Attachment is not NPU execution acceptance.

The existing driver accepts one ioctl, number 30000. It copies the LP64
VIPLite envelope `{ command, error, bytes, buffer }` explicitly. The
user pointer is a 64-bit field at offset 16; the envelope is 24 bytes.
The initial structures and version remain unchanged.

| Command | Current behavior |
| --- | --- |
| INIT (0), DESTROY (1) | Software handshake and global process accounting |
| READ_REG (2) | Bounded read after successful stage4 and fresh retained CCU/PCK proof |
| WRITE_REG (3) | NOT_SUPPORTED |
| QUERY_DATABASE (10) | Static PID/core/SRAM facts from the pinned BSP database |
| QUERY_DRIVER_STATUS (21) | Software initialization counters; power_state reports the handshake state |
| Memory, cache and task commands | NOT_SUPPORTED |

INIT does not power the NPU. The status power_state is not a measurement
of the power domain. Database data is not a silicon read. READ_REG rejects cold, failed or
stale ownership before mapping NPU registers. It holds the consumer mutex
through the proof and bounded access. It does not prepare resources.

## Acceptance probe

`ember/tools/a733-npu-vip-smoke.c` checks INIT, database facts, the exact
silicon PID `0x1000003b` from READ_REG at 0x30, and DESTROY. It returns
failure for a wrong silicon PID, either ioctl error channel, and cleanup
errors. Each successful INIT has one DESTROY attempt. Every opened file
has one close attempt. The probe keeps the existing ioctl and structure
layout. Build it for the target with the common GCC16 toolchain and a
proven EmberBSD sysroot. Run it only after current driver registration,
hardware readiness, and resource ownership have been established.

For a cold stage-0 NPU, run the software subset after driver registration:

```sh
a733-npu-vip-smoke --software-only /dev/vipcore
```

This mode issues INIT, QUERY_DATABASE, QUERY_DRIVER_STATUS, and DESTROY.
It requires a nonzero software init counter and handshake power_state 1.
It issues no READ_REG, MMIO, clock, reset, power, or DMA operation.
The default mode includes READ_REG and still requires hardware readiness.

The probe checks a diagnostic ABI and identification. It does not execute
a task or a model. A successful INIT/status response or NOT_SUPPORTED
result cannot establish accelerator execution.

## Typed resource preparation

Stages 1 and 2 query the domain and nominal clock rates without console
output. They do not establish hardware readiness. Stage 3 is a read-free
EBUSY preflight. Stage 4 resolves the fixed NPU provider
handles, reserves PCK4 and the NPU CCU slot, then retains PCK before the
first CCU write attempt. Clean acquisition failures release only these
unretained reservations. After retention, failures keep the resources.

`sun60i_a733_ccu_npu_prepare()` requires its own prior reservation. It selects
PERIPH0-600M at selector 2, clears all five divider bits, and performs the
fixed local reset/gate sequence. It preserves SRAM reset 19 and unrelated
fields. It neither changes GPU ownership nor enables PLL_NPU. GPU and NPU
can hold separate local leases while borrowing unchanged running parents.

The NPU profile requires fresh stable REF normalization, integer 600 MHz,
manual/configured/effective 600M post-gating, and the static 200 MHz AHB path.
The provider checks its own retained local sequence before ID access.
The consumer sets MMIO readiness only after the observed PID/revision match.
Public reads repeat CCU and PCK ownership proofs before NPU mapping.
These checks do not exclude firmware, authorize DMA or establish task execution.
See the [clock provider](a733-accelerator-clocks.md) and
[power provider](a733-power-domains.md).

## Source controls and physical limits

Run the standard-library host control:

```sh
python3 ember/tests/a733-npu-observe.py
```

It compiles the production NPU attach and generic FDT pre-attach bodies
with narrow host doubles. Those attachment controls supply no MMIO or
clock/reset APIs. Separate runtime controls use deterministic provider doubles.
It also compiles the complete production smoke probe and tests success,
wrong PID, both error channels, and cleanup failures. Layout assertions
compare the probe structures with the production driver structures.
The register control executes the actual 32-bit PID/revision/date reader.
It checks the 0x34-byte mapping, every read failure, and the unchanged
READ_REG offset range. The reader publishes a new identifier triple only
when all three reads succeed. Cold, failed and stale ownership cause no
NPU mapping or peek. The actual consumer sequence checks retention before
write, clean unwind, failed writes, ID faults and wrong observed IDs.
The preliminary-stage control requires no console output and checks every
missing clock handle and its reference cleanup.
The accelerator-clock control tests all 32 stale divider values, both
GPU/NPU orderings, current parent proofs and retained write failures.
These are host controls. They do not validate device access, native ioctl
copying, power sequencing, DMA, IRQ delivery, command completion, or a
model result. See the [native FE stages](a733-npu-engine.md) for the separate DMA/IRQ contract.
The [physical FE result](a733-npu-engine.md#physical-acceptance) records
the separate bounded Engine2 hardware acceptance. It does not extend
the diagnostic VIPLite subset or enable its memory, cache or task ioctls.
