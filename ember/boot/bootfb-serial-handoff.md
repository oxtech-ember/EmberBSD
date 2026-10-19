# Joined CPU console handoff contract

The manual `EMBERV3DCONSOLE` kernel profile includes `EMBERV3DHDMI` and adds
`WSDISPLAY_BOOTFB_HANDOFF`. It tests CPU console routing and drain only. It adds
no firmware ACK, HVS programming, KMS operation or hardware ownership. Default
profiles stay unchanged. A coherent HANDOFF-on kernel link and physical serial
acceptance remain pending.

The [joined issuer](boot-framebuffer-issuer.md#native-joined-boot-job) owns the
lease and terminal result. The [PLCOM readiness record](serial-console-ready.md)
owns the fixed provider hold. The [checked console](console-publication.md)
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
cancel or fall back, including after renderer setup failure. The owner remains
opaque to the renderer and grants no display, firmware, MMIO or DMA authority.
The manual `EMBERV3DCONSOLE` profile includes `EMBERV3DHDMI` and enables only this
additional CPU console transaction. Default profiles stay unchanged. The first
coherent HANDOFF-on kernel link and physical serial/HDMI acceptance remain pending.
No firmware ACK, HVS programming or KMS path is added.

```sh
ruby ember/tools/bootfb-serial-handoff-contract.rb /absolute/os/source
SKIP_MUTANTS=1 CFLAGS='-O1 -g -fsanitize=address,undefined' \
    ruby ember/tools/bootfb-serial-handoff-contract.rb /absolute/os/source
```

This runner composes actual close and PLCOM attachment fixtures. It links the real
MI issuer, FDT close, serial readiness/hold, PLCOM mutation guards, stable facade,
`cnopen`, config pending/join/finalizers, native job and native finalizer bodies.
Kernel scheduling, FDT, bus, tty and vnode services are controlled. It checks both
finalizer orders, exact previous backend and lease, output canaries, cold/warm and
reader contexts, open dispatch, allocation/barrier holds, generation commits,
pre-freeze fallback, terminal faults and unknown-status retention. A controlled
atomic-load hook freezes publication between poll admission load and CAS. A real
checked console reservation and cnattach, plus the exact cdev commit statements,
exercise competing publication during allocation. The rollback preserves that
new cdev and generation. It compiles the
actual conditional ACPI attachment declaration with the option off and on.

`CC`, `CFLAGS`, one-executable `TEST_RUNNER`, `COMPILE_ONLY=1`, `SKIP_MUTANTS=1`,
`SERIAL_HANDOFF_MUTANTS` and `TMPDIR` select execution. `SERIAL_HANDOFF_EVIDENCE`
names a new directory. It saves exact source hashes, extracted inputs, commands
and raw outputs. Controls must compile and reach their named CHECK. No target ABI,
real MP scheduling, physical serial session or display ownership is established.
