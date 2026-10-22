# RTL8723DS diagnostic transport

The [native attachment](../rtl8723ds_probe.c) provides a guarded one-shot
CMD52 diagnostic. It creates no network interface. Physical observations and
radio limits belong to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards/blob/main/boards/ys-m33-a133/interfaces.md).

## Match and read contract

Require SDIO function1, manufacturer 0x024c, product0xd723, interface0x07, a pure
I/O card and the actual FDT MMC1 parent. Use the common function0 CIS, not the
possibly absent per-function MANFID. Validate the common-CIS owner and function0
identity. The host must have the translated resource ownership marker and both
zero-length root opt-ins: `ember,ys-m33-sdio-probe` and
`ember,ys-m33-rtl8723ds-read-probe`. Absent or malformed opt-ins perform no new I/O.

The [read helper](../rtl8723ds_read.c) validates domains before encoding CMD52.
Local domain0x10250000 permits offsets through0x0fff. MAC domain0x10260000
permits offsets through0xffff and maps to CMD52 address `0x10000 | offset`.
Reject a word that crosses a domain boundary. Report host and card R5 errors;
commit a little-endian 32-bit output only after all four byte reads succeed.

Attachment reads CCCR enable/ready, MAC SYS_CFG1/SYS_CFG2 and local HCI suspend
control, then repeats SYS_CFG1 and requires equality. Stop at the first error.
The read opt-in does not enable a function, issue CMD53, change block size, load
firmware, install an IRQ, access packet FIFOs or change radio power. Diagnostics
report stage and errno; exact failed-byte arguments/raw R5 flags are not exposed.

## Separate function lifecycle

A third zero-length opt-in, `ember,ys-m33-rtl8723ds-function-probe`, enables the
[checked function lifecycle](../rtl8723ds_function.c) after read preflight.
Only its allowlisted CMD52 writes are admitted; exchange operations are refused.
The attachment runs on the SD/MMC task thread without an asynchronous handler.
It saves IOEx, enables function1, waits for ready with a bounded helper, checks
SYS_CFG1 again and attempts restoration before returning. Restore errors remain
errors even if earlier reads succeeded. This is not chip power initialization,
firmware loading, association or a WLAN data path.

## Checks and source authority

Run from the OS root:

```sh
sh ember/tools/rtl8723ds-read-test.sh
sh ember/tools/rtl8723ds-function-test.sh
```

The read fixture executes the production native match body. It covers common-CIS
ownership, malformed/default/foreign FDT/card matches, domain boundaries, byte
order, R5 failures, partial-read failures and unchanged output on error. The
function fixture checks bounded enable/restore and write admission. These source
contracts do not replace physical enumeration or restoration evidence.

Protocol definitions follow Linux v6.12 commit
`adc218676eef25575469234709c2d87185ca223a`, rtw88 `sdio.c`, `sdio.h`, `reg.h` and
`rtw8723ds.c`, using their BSD-3-Clause license option with retained authorship.

## Provenance

Contracts consolidated from these documents at OS revision `4a4c98bb99999836103d3fe63fd6f4c98013f0c5`.
Git retains their original text; the hashes identify the inputs.

- [2026-10-08-rtl8723ds-read-design.md](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/docs/superpowers/specs/2026-10-08-rtl8723ds-read-design.md): SHA256 `371c87038121ddd5f17b5988f56695b406be4cd0883cb4e0d01819e8ab060297`.
