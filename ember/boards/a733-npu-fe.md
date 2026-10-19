# A733 NPU read-only FE admission

Engine1 connects the native MMU/DMA mechanism to the NPU device driver.
It takes a non-destructive register snapshot and prepares an owned command
context. It does not write NPU registers, enable an IRQ, synchronize DMA maps
or start the FE. It does not prove task or model execution.

Cold attachment stores the FDT DMA tag. It does not map the NPU, allocate a
context or register an IRQ. Existing power stages 0 to 4 and the VIP ioctl ABI
stay unchanged. Engine preparation requires successful powered identification
at power stage 4, the expected PID/revision and fresh matching retained CCU/PCK
ownership. The same owner cookie is used throughout.

The privileged `hw.sun60inpuid0.engine_stage` control accepts 0 for a dry
request and 1 for preparation. It uses the existing run mutex. A cold or stale
request fails before a NPU mapping or allocation. After an admitted attempt,
a repeated request returns `EALREADY` without more reads or allocations.
A dry request does not reset the engine phase or release a context.
A failed preparation revokes the earlier legacy MMIO-ready flag. Successful
read-only preparation restores it after all checks. The READ_REG gate and
offset window are unchanged. Cached raw evidence remains available on failure.

## Bounded snapshot and admission

Two complete peek32 samples read exactly these offsets from the 4096-byte
NPU register window:

`0x000, 0x004, 0x014, 0x024, 0x030, 0x388, 0x3a4, 0x3b4, 0x654, 0x664, 0x668, 0x66c`

IRQ status `0x010` is excluded. The vendor handler reads it during IRQ
servicing without a separate ACK write. Read-clear behavior is an inference,
so this admission path does not use it as an ordinary observation register.

A map or peek failure publishes no partial snapshot. Fresh CCU/PCK proofs
precede the map and follow the complete samples. The mapping is always closed
before the function returns. A complete snapshot can be valid while admission
fails. Admission requires equal samples, PID `0x1000003b`, revision `0x9202`,
all-module idle `0x7fffffff`, IRQ enable zero, MMU/PD enable bits 0/5 clear and
FE command-control enable bit 16 clear. FE idle alone does not exclude NN work.
Unexpected active or unstable state returns `EBUSY` before allocation.

## Owned memory and fixed bytes

After admission, the driver creates the [native low32 DMA context](a733-npu-mmu.md).
The command VA is `0x01001000` and must differ from the command DMA address.
The sole command image contains these little-endian 32-bit words:

`0x08010e01, 0x00000044, 0x10000000, 0x00000000`

These encode event 4, block bit 6, then END. The rest of the mapped command
page is zero. Encoding these bytes does not accept their execution semantics.
The context stays unretained and unsynchronized during Engine1. Its opaque
info exposes root, STLB and command DMA addresses as values; no CPU pointer is
exported. A clean preparation error destroys only the new context. If clean
release fails, the driver preserves the pointer and records that release error.

The cached nodes under `hw.sun60inpuid0` are:

- `engine_stage`, `engine_result`, `engine_attempted`, `engine_prepared`.
- `engine_snapshot_valid`, `engine_release_error`.
- `engine_mtlb`, `engine_stlb`, `engine_command_dma`, `engine_command_va`,
  `engine_pd`, `engine_command_bytes`.
- `engine_s0_rXXX` and `engine_s1_rXXX`, where `XXX` is each offset above
  without `0x`, padded to three hexadecimal digits.

Cached reads take the run mutex and perform no register access. Raw words use
32-bit INT nodes; addresses use 64-bit QUAD nodes. `engine_snapshot_valid`
means complete raw data. `engine_prepared` means CPU context preparation passed.
Neither flag replaces a fresh hardware ownership check before a future start.

## Provenance and next boundary

The primary format facts are in VIP2 BSP commit
[2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/npu/aw_nna_vip/vip2):
`vip_drv_hardware.h` defines the all-module idle value;
`vip_drv_hardware.c:cmd_event` defines event word encoding;
`inc/vip_drv_share.h` defines event ID 4 and 8-byte EVENT/END sizes;
`os/linux/vip_drv_device_driver.c` shows IRQ status servicing.
The Linux implementation is not copied. This is original BSD code from facts.

Run `python3 ember/tools/a733-npu-fe-contract.py`. Its variables are `CC`,
`NPU_FE_CFLAGS` and `NPU_FE_SOURCE_ROOT`. The runner tests the complete snapshot
helper, pure encoder/admission functions and actual consumer/readiness bodies.
The native DMA module is checked separately through its real API lifecycle
fixture. These host checks are not physical task execution.

Engine2 must separately prove a finite local-init/PD write list, IRQ ownership,
stale-status handling, fetch bounds inside owned memory, retention before the
first attempt and new event plus terminal evidence. It must block public
READ_REG before map after publication. This change adds no Engine2 path.
