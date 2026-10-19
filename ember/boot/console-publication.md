# Checked console publication foundation

`WSDISPLAY_BOOTFB_HANDOFF` is default-off. It selects
`WSDISPLAY_BOOTFB_DEFER` and the early framebuffer reader hooks. No ordinary or
experimental kernel configuration enables this new option. There is no native
caller, close/drain actuator, replacement output sink or hardware operation.

## Publication contract

The private [console counter](../../sys/dev/cons_handoffvar.h) records active
publication operations. It does not serialize their binding updates. Runtime
binders also hold Giant; selected early preattach uses bootstrap serialization.
Its CLOSED state only refuses new binding publication.
It does not stop a reader, drain a callback, close a poll session or detach a
device. The foundation has no production caller of `cn_publication_freeze`.
It does not make CLOSED a display ownership grant.

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
The existing TOLOG/msgbuf behavior is unchanged. Direct `cnputc` and TOCONS-only
output is not redirected. Reader close/drain, a stable post-close sink and the
exact owner actuator remain separate from this foundation.
