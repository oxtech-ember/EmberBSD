# Joined CPU console and firmware notification contract

The manual `EMBERV3DCONSOLE` kernel profile includes `EMBERV3DHDMI` and adds
`WSDISPLAY_BOOTFB_HANDOFF`. The joined boot job holds the serial console, closes
and drains CPU framebuffer access, then sends one firmware display notification.
Default profiles stay unchanged. This stage adds no HVS programming, KMS operation
or display ownership. The CPU close and firmware notification pass coherent GCC16 linkage and
physical CM5 boot checks. The
separate [two-pipeline continuation](../../../arch/arm/broadcom/docs/bcm2712-native-display-quiesce.md) has source
checks; its native link and physical acceptance remain pending.

The [joined issuer](boot-framebuffer-issuer.md#native-joined-boot-job) owns the
lease and terminal results. The [PLCOM readiness record](../../../arch/evbarm/dev/docs/serial-console-ready.md)
owns the fixed provider hold. The [checked console](../../wscons/docs/console-publication.md)
keeps its stable facade and real reader barrier.

With that option on, the same joined job calls `simplefb_console_close_serial`.
The issuer validates its exact CLAIMED lease and MI's actual previous console.
The fixed built-in PLCOM ACPI provider must have its real tty, cdev and both
interrupt services ready. Its hold precedes KM_SLEEP and the Giant-dropping reader
barrier. The stable wsdisplay facade sets the validated serial `cn_dev` before
freeze. The CPU close then drains the early framebuffer readers. CLOSED kernel
putc/getc/pollc use that same held previous backend, independently of MULTICONS.
No console vnode or redirected tty may already exist. The real pending join ends
before either finalizer and before init's console open.

A refusal before freeze releases the provider and cancels the lease for the one
genfb fallback. CLOSED and unknown admitted outcomes retain the owner. They never
cancel or fall back, including after renderer setup failure. The renderer borrows
that opaque owner. This pointer grants no hardware or firmware authority.

After a successful complete serial close, the private native owner checks the
exact simplefb terminal record and MI's issued CLOSED lease with its held serial
route. A raw CPU close cannot pass this gate. The owner then calls the actual
[notification helper](../../../arch/arm/broadcom/docs/bcm2712-display-handoff.md) through its fixed mailbox
transport. It retains attempted/complete/fault for this boot. Any failed gate,
transport error, malformed reply or unsupported tag stops this stage without
retry, fallback or hardware admission. The first owner error stays unchanged.
An ACK proves no hardware exclusion, scanout drain or display ownership.

```sh
ruby ember/tools/bootfb-serial-handoff-contract.rb /absolute/os/source
SKIP_MUTANTS=1 CFLAGS='-O1 -g -fsanitize=address,undefined' \
    ruby ember/tools/bootfb-serial-handoff-contract.rb /absolute/os/source
```

This runner composes actual close and PLCOM attachment fixtures. It links the real
MI issuer, FDT close, serial readiness/hold, PLCOM mutation guards, stable facade,
`cnopen`, config pending/join/finalizers, native job, fixed transport and protocol
helper. Kernel scheduling, FDT, bus, tty, vnode and mailbox services are controlled.
The mailbox DMA/quarantine implementation has its separate
[actual-source contract](../../../arch/arm/broadcom/docs/bcmmbox-transactions.md).

Checks cover both finalizer orders, exact previous backend and lease, contexts,
open dispatch, allocation/barrier holds, generation commits, pre-freeze fallback,
terminal CPU faults and unknown-status retention. A controlled atomic-load hook
freezes publication between poll admission load and CAS. A real checked console
reservation and cnattach, plus the exact cdev commit statements, exercise competing
publication during allocation. Rollback preserves the new cdev and generation.
Firmware checks cover invalid close records, failed status queries, missing holds,
foreign leases, raw closes, re-entry, repeated jobs, transport errors, malformed
and unsupported replies, and zero I/O on refusal. The runner also compiles the
actual conditional ACPI attachment declaration with the option off and on.

`CC`, `CFLAGS`, one-executable `TEST_RUNNER`, `COMPILE_ONLY=1`, `SKIP_MUTANTS=1`,
`SERIAL_HANDOFF_MUTANTS` and `TMPDIR` select execution. `SERIAL_HANDOFF_EVIDENCE`
names a new directory. It saves exact source hashes, extracted inputs, commands
and raw outputs. Controls must compile and reach their named CHECK. No target ABI,
real MP scheduling, physical serial session or display ownership is established.
