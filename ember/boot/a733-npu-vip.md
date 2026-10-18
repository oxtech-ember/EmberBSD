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
| READ_REG (2) | Bounded NPU MMIO read; hardware readiness must be established separately |
| WRITE_REG (3) | NOT_SUPPORTED |
| QUERY_DATABASE (10) | Static PID/core/SRAM facts from the pinned BSP database |
| QUERY_DRIVER_STATUS (21) | Software initialization counters; power_state reports the handshake state |
| Memory, cache and task commands | NOT_SUPPORTED |

INIT does not power the NPU. The status power_state is not a measurement
of the power domain. Database data is not a silicon read. READ_REG does
not establish clocks, power or ownership before access. Do not use it as
a probe on an unpowered NPU.

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

## Shared-resource limit

`sun60i_a733_ccu_npu_prepare()` selects the running PERIPH0-600M parent.
It does not enable PLL_NPU. The existing coarse GPU clock lease rejects
this call with EBUSY while another GPU owner is retained. NPU preparation
also sets that lease's attempted flag without acquiring its owner. This
is not a complete NPU reservation. Keep the guard in place until separate
local GPU/NPU ownership and shared parent borrowing have source controls.
PLL_NPU can feed memory and video consumers; it must not be retuned for
this diagnostic path. See the [power provider](a733-power-domains.md) and
[GPU ownership](a733-gpu-identification.md).

## Source controls and physical limits

Run the standard-library host control:

```sh
python3 ember/tests/a733-npu-observe.py
```

It compiles the production NPU attach and generic FDT pre-attach bodies
with narrow host doubles. No NPU MMIO or clock/reset APIs are supplied.
It also compiles the complete production smoke probe and tests success,
wrong PID, both error channels, and cleanup failures. Layout assertions
compare the probe structures with the production driver structures.
These are host controls. They do not validate device access, native ioctl
copying, power sequencing, DMA, IRQ delivery, command completion, or a
model result. The earlier [board evidence](../boards/orange-pi-zero-3w.md)
remains a separate hardware cohort.
