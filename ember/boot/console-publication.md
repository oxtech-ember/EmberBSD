# Checked console publication and present-lease drain

`WSDISPLAY_BOOTFB_HANDOFF` is default-off. It selects
`WSDISPLAY_BOOTFB_DEFER` and the early framebuffer reader hooks. The explicit
`EMBERV3DCONSOLE` profile enables its joined CPU console transaction. Default
profiles stay unchanged. The exact-lease close and retained serial route are
source-tested. A coherent HANDOFF-on kernel and physical handoff remain pending.
No display or firmware operation is authorized.

## Publication contract

The private [console counter](../../sys/dev/cons_handoffvar.h) records active
publication operations. It does not serialize their binding updates. Runtime
binders also hold Giant; selected early preattach uses bootstrap serialization.
Its CLOSED state refuses new binding publication and admitted old input. The
counter alone does not drain a reader or detach a device. The explicit actuator
uses a separate real reader barrier. CLOSED is not a display ownership grant.

The checked profile exports errno-returning `wsdisplay_cnattach_checked`,
`wsdisplay_preattach_checked`, `wsdisplay_predetach_checked`,
`wsdisplay_cndetach_checked` and `cn_set_tab_checked`. It does not export their
old unchecked symbols. The option-off profile keeps the old symbols and paths.

Regular frontend admission takes Giant and records the actual device, thread
and nesting depth. The selected parent and nested genfb use the same device.
A different device or thread refuses before frontend effects. Giant can be
released during a sleep. The recorded operation remains active during that
interval; Giant alone is not its lifetime proof.

Regular checked attach requires process context, Giant and that same-thread
reservation. Checked detach requires process context, Giant and no active
frontend reservation. The generic checked setter requires process context and
Giant. It refuses an active publication without a dependency on wscons.
The early checked preattach is limited to the selected serialized cold bootstrap
path. Its reservation covers the map, preattach, complete replay and READY
publication. An error does not publish capture or READY.

## Selected parents and retained state

The selected genfb and VirtIO frontend reserve before vcons, clearing, DMA or
scanout effects. Simplefb, the BCM framebuffer parent, the PrimeCell parent and
drmfb reserve before their console map or initialization work. A rejected early
reservation performs none of that work. A late failure returns an error; it is
not proof that an initialized renderer or its storage can be destroyed.

`wsdisplay_emul_attach_checked` calls the real `config_found_acquire` under Giant.
Console match and attach require the same scoped parent, auxiliary object and
thread. The child callback records its actual completion and errno. A non-NULL
device alone is not success. Successful return transfers one acquired reference;
the caller must release it or preserve its matching ownership lifetime.
The selected UDL path remains explicitly nonconsole and keeps its existing weak
parent-child association after it releases that acquired reference.

On failed completion, a read iterator protects the known child allocation.
`config_detach_release` consumes its input reference on every return. No extra
localcount reference is held across a successful drain. After a detach error,
the helper reacquires the driver/unit and checks exact identity before softc
access. An exact live failed child retains that reference and both errors.
Its detach callback refuses before effects while that self-reference remains.
There is no automatic recovery. A vanished or replaced object is not dereferenced
as the former child. An unexpected foreign child remains in autoconfiguration;
the helper releases its input reference and reports a protocol error.

The five selected serial attachment writers update their own console descriptor,
not a foreign `cn_tab->cn_dev`. A selected COM console refuses detach before
disable under this profile. The existing PLCOM console refusal remains intact.
This finite source selection is not a migration of every architecture or a
general console hotplug contract.

## Source checks

```sh
ruby ember/tools/console-publication-contract.rb /absolute/os/source
CONSOLE_SKIP_MUTANTS=1 CFLAGS='-O1 -g -fsanitize=address,undefined' \
    ruby ember/tools/console-publication-contract.rb /absolute/os/source
```

The runner compiles complete actual publication, reservation, selected child
and checked mutator bodies. It also compiles the exact changed serial statement
blocks in fixture wrappers. Those wrappers are test glue, not copied driver
implementations. Kernel device, reference, iterator, Giant, mux, screen, emulator
and PMF services are controlled. The reference service models the real API's
consumed-input rule; it does not execute a real kernel detach or collector.
The foreign-mutator case models another thread holding Giant while the owner
sleeps. It is not a kernel MP scheduling test.

`CC`, `CFLAGS` and one-executable `TEST_RUNNER` support cross compilation and
execution. `CONSOLE_COMPILE_ONLY=1` compiles without execution.
`CONSOLE_SKIP_MUTANTS=1` runs the positive contract. `CONSOLE_MUTANTS` selects
named controls. `CONSOLE_CASE=foreign-mutator` selects the isolated admission
regression. `CONSOLE_EVIDENCE` must name a new directory. It records exact source
hashes, extracted inputs, commands and raw output. Mutants must compile and reach
an actual CHECK/SIGABRT. A different crash, compile error or timeout is refusal
of the test run, not successful mutant rejection.

Native checks must use the actual selected per-file flags, matching headers and
an option header produced by `config`. The selected old-symbol closure must fail
if an additional publisher is selected. Set `CONSOLE_SELECTED_LIST` to a JSON
array of actual selected C source paths, relative to the source root or absolute
for generated C. `CONSOLE_SELECTION_ONLY=1` runs only that guard. It checks the
reviewed five direct paths, three frontend parent paths, four genfb parent paths
and known console binders, including conditional branches. It refuses a new
publisher. This lexical check is separate from target object symbol closure.
Host controls, isolated compilation and
static analysis do not establish a coherent kernel link or physical handoff.

## Exact present-lease close

The [FDT wrapper](../../sys/dev/fdt/simplefbvar.h) accepts only a real issued
`wsdisplay_bootfb_lease` for the exact resident early binding. Call
`simplefb_console_close` in process context. Do not hold an owner, callback,
printf or spin lock across it. This contract permits Giant recursion; the writer
drops all Giant levels for `pserialize_perform` and restores the same depth.
It creates a real local pserialize handle before irreversible admission.
Its KM_SLEEP allocation can yield, so the issuer revalidates the lease afterward.

Before freeze, an active publisher, old getc, poll session or initial replay
returns EBUSY. The lease remains CLAIMED and cancelable. After freeze, cancel,
fallback and new checked publications refuse. Actual binding commits supply the
saved generation. The writer checks the same binding and generation after the
barrier. Panic can make the real barrier return without draining; that outcome
remains closed with an error. No reopen, detach, unmap or retry is provided.

Actual cnputc checks CLOSED inside its early pserialize section before the
emulator and old MULTICONS callbacks. Each standalone old getc holds separate CAS
admission until the callback returns; it can block and is not a pserialize
reader. A poll pair holds admission through its final old callback. A closed
pair still balances keyboard callbacks, and closed getc keeps keyboard input.
The external cons.c poll refcount, preemption and heartbeat logic is unchanged.
Whole initial replay keeps publisher admission through final vcons update and
READY. It cannot race a valid issued close. The additional CLOSED replay guard
is defensive; its fixture fault injection bypasses the normal freeze protocol.

`simplefb_console_close_status` returns cached value facts: generation, separate
MI and MD errors, unified error, attempted, closed, drained and complete. Invalid
queries leave output unchanged. The FDT wrapper checks the real association
before MI close and rematches it after a successful MI drain. A failed rematch
is a sticky unified failure even if the descriptor later recovers. Valid-context
repeats for the same terminal lease return its original result without another
barrier or rematch. Concurrent close refuses with EBUSY.

The early READY/type/cookie metadata, descriptor, mappings, emulator and device
remain resident. The exact metadata remains matchable after CPU output closes;
it does not restore cancel or output authority. The cn_tab pointer is not swapped.
The existing TOLOG path records a character in msgbuf before v_putc. Closed output
does not call printf or logputchar again. The raw close API alone does not redirect
direct cnputc, TOCONS-only or DDB output, and does not change `cn_dev`.

The joined `simplefb_console_close_serial` transaction adds the fixed held PLCOM
backend. MI validates its own previous console and checks that no console vnode
or redirected tty exists. It commits the validated serial `cn_dev` before freeze.
CLOSED putc/getc/pollc route to that same backend. The route is published before
freeze and is immutable after CLOSED. Existing old getc and active poll admission
still exclude freeze. Pre-freeze rollback restores NODEV only for its own generation, cdev and early
binding. It preserves a competing checked console commit and releases the hold.
A poll pair records its backend before callbacks. Failed admission selects the
held serial route before any keyboard callback; off uses the same recorded route.
Each cn_dev commit advances the actual binding generation. A terminal raw close
cannot be upgraded into a successful unheld serial route. The source fixture
executes real cnopen dispatch against controlled vnode/tty services. It does not
establish a physical serial session. See [the boot transaction](boot-framebuffer-issuer.md#native-joined-boot-job).
A CPU drain proves neither firmware/audio/DMA/scanout exclusion nor power or
MMIO accessibility. ENXIO, no framebuffer and a NULL owner do not grant access.
Future verified-absence admission is a separate typed contract. Boot GOP is not
an assumed permanent prerequisite for headless or hotplug operation.

```sh
ruby ember/tools/console-close-contract.rb /absolute/os/source
SKIP_MUTANTS=1 CFLAGS='-O1 -g -fsanitize=address,undefined' \
    ruby ember/tools/console-close-contract.rb /absolute/os/source
```

This fixture compiles the real issuer, checked bootstrap, output, getc, poll,
writer, FDT association matcher/wrapper, vcons replay and TOLOG putone bodies.
Only the three unchanged unused callback dev parameters have fixture-only unused
attributes; their bodies retain the original bytes. Kernel Giant, allocation,
pserialize, context, device and FDT parser/platform services are controlled.
The pthread grace-period model is not a kernel MP or physical scheduling test.
The original reader, issuer and publication positives remain separate regressions.
`CONSOLE_CLOSE_EVIDENCE` names a new output directory. `SKIP_MUTANTS=1` skips source
mutations. `MUTANT` selects comma-separated controls. `COMPILE_ONLY=1` performs
cross compilation without execution. CC/CFLAGS and one-executable TEST_RUNNER
apply to both positives and mutants. Each mutant must compile and reach an actual
CHECK/SIGABRT; a compile error, other crash or timeout refuses the run.
