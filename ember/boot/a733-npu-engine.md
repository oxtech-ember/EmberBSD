# A733 native NPU FE stages

The native stages use one fixed A733 NPU instance. They do not implement
VIPLite task or model ioctls. AI assistance was used for the implementation
and its source controls. See the [attachment and resource contract](a733-npu-vip.md).

## Read-only admission

`hw.sun60inpuid0.engine_stage=1` requires successful power stage 4, the exact
PID `0x1000003b`, revision `0x9202`, and fresh retained CCU/PCK ownership.
It revokes legacy readiness before admission. It reads two samples of the
12 fixed engine registers. It never reads IRQ status at 0x010.

Admission requires stable samples, all modules idle (`0x7fffffff`), IRQ
enable zero, MMU bits 0 and 5 clear, and FE enable bit 16 clear. It then
allocates a native 24 KiB DMA context: one MTLB page, four STLB pages, and
one command page. Each loaded map must be non-bouncing, low32, one full
segment, and disjoint from the other two maps. Command VA `0x01001000`
must differ from command DMA PA. The 16-byte event/END image is not synced,
registered with an IRQ, or submitted. A failed attempt never restores
legacy readiness. A clean failure frees only the new unretained context.
A failed release keeps its pointer and error.

## One retained FE run

`engine_stage=2` requires a successful Engine1 context. It permits one
attempt. It closes legacy READ_REG access before mapping the NPU. A later
power-stage request cannot reopen READ_REG after publication is attempted.
The helper checks the fixed level-high SPI65 tuple, its enabled GICv3
parent, three interrupt cells, and exact parent register ranges before MMIO.
It uses the standard shared-capable `fdtbus_intr_establish_xname` API.
This is not an exclusive IRQ reservation. The supported board tree has
one NPU node and no other SPI65 consumer. A changed tree needs a new route
audit. No FDT, GIC, or PIC framework behavior changes.

The helper reads two stable samples of the original 12 registers plus
0x090, 0x100, 0x104, 0x10c, 0x3a8, and 0x414. It also requires isolation,
MMU load, and internal reset bits clear. It replaces the command page with
one fixed 40-byte little-endian MMU flush, semaphore, stall, event4, and END
image. Replacement is allowed only before retain. The trusted image and
all native DMA mappings remain stable.

DMA retention is sticky before the first PREWRITE sync. IRQ registration
occurs after retain. Fresh CCU/PCK proofs precede sync, local writes, and
FE start. The handler returns zero without MMIO while unarmed. Local init
uses a finite private operation table. PD comes from the owned MTLB map.
The command address uses VA, not PA. Fetch control `0x00010200` bounds
prefetch to the owned 4096-byte command page. Register readbacks are literal
or use the specified RMW mask. An unexpected value is a refusal.

The IRQ handler reads only the owned NPU status at 0x010. It uses ordinary
bus-space operations, not fault-recovering peek. The first nonzero status
is recorded. The handler writes IRQ enable 0x014 to zero, issues a WRITE
barrier, and reads 0x014 to drain the mask before GIC EOI. A nonzero mask
readback is a fault. The handler takes only its private IPL_VM spin mutex.
It does not allocate, print, sleep, or call CCU/PCK/FDT lifecycle methods.
The FE write and its WRITE barrier hold the same spin mutex. An event
before the start attempt cannot establish completion.

Completion requires a new post-start event4 IRQ, no AXI/MMU or unexpected
bits, two all-idle samples, and two equal DMA-address samples within the
owned command VA page. Idle alone is insufficient. At most 100 polls use
1000 microseconds of delay each. Scheduling and interrupt latency can add
to this bound. The thread then masks and drains IRQ enable and reads final
owned status. A late error refuses completion. Offset 0x664 is recorded
as a DMA address; it is not claimed to be a program counter.

After publication is attempted, mapping, DMA, IRQ storage, and resource
leases survive success, faults, and timeout until reboot. There is no
reset, retry, stop, or release operation. A failed mask remains a fault;
it does not establish that the interrupt line is quiescent.

## Cached evidence and verification

`engine_result` records the stage return value. `run_attempted`,
`run_publication_attempted`, `run_retained`, `run_started`, `run_terminal`,
`run_snapshot_valid`, `run_step`, `run_error`, `run_last_readback`, and
`run_command_bytes` describe the last call. `run_s0_rNNN` and `run_s1_rNNN`
contain its pre-run samples. The IRQ, stale-status, terminal-idle, and
terminal-DMA fields record the terminal decision. `run_irq_count` and
`run_irq_baseline` are 64-bit counters. These sysctl nodes copy cached
state. They do not access hardware or take a new live IRQ snapshot.

Run `python3 ember/tests/a733-npu-observe.py` for source controls. It also
runs the actual native Engine2 helper with provider, DMA, FDT, and MMIO
doubles. The controls cover retention, every bounded access failure,
readback mismatch, unstable admission, pre-start IRQs, posted-write drains,
spurious events, late faults, timeout, and false terminal evidence.

The local init and command format facts come from VIP2 commit
`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`, `vip_drv_hardware.c` and
`vip_drv_mmu.c`. The adapted local sequence preserves the Vivante MIT
notice. Unmapped MTLB exception value 2 remains a source-format choice
that requires hardware proof. Source controls and successful compilation
do not establish IRQ delivery, MMU translation, FE completion, NN execution,
or a model result. The fixed FE stream contains no NN model operation.
