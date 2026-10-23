<!-- Origin: EmberBSD native RTL8822C TX fence contract, AI-assisted. -->
# RTL8822C synchronous TX fence

`rtw88_tx_quiet()` runs under the caller's PCI serialization lock.
It does not sleep. It first closes host producers and invalidates rate
adaptation. Experimental RFK power and operating seals become invalid too.

The helper writes `REG_TXPAUSE` (`0x0522`) to `0xff`. It separately clears
`REG_CR.MACTXEN` (`0x0100`, bit 6), preserving the other CR bits.
It reads back both controls. A failure of one control does not skip the other.
The first error is returned. A write error can occur after a device side effect;
the helper does not retry, restore TX controls, or reopen host admission.

The register definitions come from [FreeBSD rtw88 reg.h at
a143197801c680b958ab662c88cf086668a1958b](https://github.com/freebsd/freebsd-src/blob/a143197801c680b958ab662c88cf086668a1958b/sys/contrib/dev/rtw88/reg.h).
That reference carries Realtek's `GPL-2.0 OR BSD-3-Clause` notice.
The native implementation uses the BSD option and imports no LinuxKPI code.

An inactive core returns `ENXIO` without device I/O and retains the host fence.
Any error leaves hardware quiet unproven. The caller must keep admission closed
and use the existing process-context stop owner. The helper does not free DMA
buffers, change queue ownership, or claim that a transmitted frame was recalled.
Successful register readback is not physical proof of RF silence.

All five queues report no capacity after the fence. Raw TX, firmware commands,
station configuration, loopback, and channel programming reject new work.
The core fence can clear only through the existing full stop/start lifecycle.
A wrapper that needs a persistent spectrum restriction must separately prevent
automatic restart. This API alone does not advertise DFS support.

An active RFK transaction also observes the host fence. Its restoration writes
cannot reopen TX after this fence. An incomplete transaction can retain RFK
quarantine and require a physical cold reset.

## Executable contract

Run `CC=gcc-16 sh ember/tools/rtw88-start-contract.sh` from the source root.
The actual core runs against the existing bus and DMA model.
Cases cover inactive hardware, success, each control's write failure before and
after mutation, initial CR read failure, each readback error or mismatch,
and two different errors in one call to verify first-error retention.
They check independent controls, all queue fences, no delay or DMA release,
and explicit full stop/start recovery. This is software contract evidence.
