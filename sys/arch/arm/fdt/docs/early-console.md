# Generic ARM early serial console

`EARLYCONS=com32` selects the firmware-configured 16550 transmitter.
Use it with `CONSADDR` and the generic ARM FDT platform.
The UART must have little-endian 32-bit registers at four-byte strides.
This is the `reg-shift = 2`, `reg-io-width = 4` layout.
The configured address must match the FDT stdout UART.
The helper does not set the baud rate, clocks, resets or pin routing.

Before the device map is ready, the helper uses the physical address.
After the map is ready, it uses the generic platform's UART page at
`KERNEL_IO_VBASE`. The mapping does not use the fixed Sunxi window.
The existing `sunxi` early console keeps its platform-specific address choice.
Both paths use `dev/ic/com_early.h` for the same 16550 transmit operation.
This helper waits for `LSR_TXRDY`, then writes one byte in a 32-bit word.
An inaccessible or unready UART can still stop early output.
The helper cannot diagnose its own MMIO or firmware setup failure.

## Diagnostic kernel profile

`EMBER64_A733_DIAG` includes `EMBER64` and sets `EARLYCONS=com32`,
`CONSADDR=0x02500000`, `VERBOSE_INIT_ARM` and `BOOT_ARGS="-s"`.
It removes `MODULAR` and `MODULAR_DEFAULT_AUTOLOAD`.
Built-in drivers remain available. External `.kmod` files cannot be loaded.
Build it with the native `build.sh kernel=EMBER64_A733_DIAG` target and the
same explicit GCC16 toolchain settings as the normal kernel.
Use a separate configuration directory under the common kernel object root.
No module bundle is needed for this diagnostic profile.

Use the [separate diagnostic DTB](../../sunxi/docs/a733-boot-modes.md).
Single-user mode alone does not prevent native GPU or KMS mountroot startup.
The compiled `-s` flag persists when the FDT boot arguments are parsed later.
It stops the normal init(8) path before multi-user services.
It does not stop kernel autoconfiguration or built-in drivers.

The first standard AArch64 early message precedes the EL1 transition.
Verbose messages then cover MMU setup, FDT bootstrap and `consinit`.
Without early console support, silence does not locate a fault before `consinit`.
Source and host checks do not establish a successful physical boot.
See [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/a733-zero3w/boot.md)
for loader selection, media applicability and hardware limits.

## Checks

Run `python3 ember/tools/arm-earlycons-contract.py` with host `CC` set to GCC16.
The test compiles the actual helper, both address selectors and the AArch64
boot-argument parser. It checks physical and virtual addresses, the unchanged
Sunxi mapping, register stride, ready-bit polling, byte writes and single-user
flag retention. It also compiles the selectors without `CONSADDR`.
Use `EARLYCONS_CFLAGS` for analyzer or sanitizer flags.
Use `--emit DIRECTORY` to retain the generated host fixture for analysis.
Host tests model MMIO and the map-ready predicate. Native configuration and
kernel linkage remain separate checks. Physical MMIO remains unverified.
