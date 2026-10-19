# Resident PLCOM console readiness

This private kernel interface describes one real attached serial tty. It is
compiled only with the default-off `WSDISPLAY_BOOTFB_HANDOFF` option. No kernel
configuration selects that option. This slice has no consumer and performs no
console routing, framebuffer close, display handoff or new hardware operation.
It does not establish a boot console-open or physical serial acceptance result.

## Provider and validation

The [private API](../../sys/arch/evbarm/dev/plcom_consolevar.h) exposes an opaque
resident record. Only the actual generic ACPI, deferred ACPI and FDT attachment
paths call `plcom_console_attach_complete` after their hard interrupt service
returns a real cookie. This producer hook is not a general permission setter.
A cookie, address, device name or caller boolean alone cannot publish readiness.
The actual common attachment has already called `tty_attach`, established the
soft interrupt, set the console cdev and completed `PLCOM_HW_DEV_OK`.

Publication and later validation check the actual driver/unit lookup, device,
tty, normal tty operations, receive buffer, soft interrupt, active/enabled state,
console instance and its real cdev number. KGDB ownership refuses readiness.
The record stores that device/softc/tty/cdev and both actual interrupt identities.
It does not infer readiness from the saved previous console pointer.

Call `plcom_console_ready_get` and `plcom_console_ready_validate` in warm,
sleepable process context with Giant held. Panic, IRQ, soft IRQ and cold contexts
return `EWOULDBLOCK`; a missing Giant returns `EDEADLK`. Invalid output storage
returns `EINVAL`. A foreign token or previous console identity returns `EPERM`.
Unpublished, revoked or changed provider state returns `ENXIO`. An error leaves
caller output unchanged. Successful validation returns the real serial `dev_t`.
The previous console argument is compared with the provider's actual `plcomcons`
identity. It is never dereferenced as caller-supplied authority.

Neither query changes `cn_tab`, `cn_dev`, interrupt state, mappings or policy.
Neither query makes a tty open succeed without normal driver semantics.
The consumer must independently prove that this provider is its recorded previous
backend. An arbitrary `wsdisplay_ocn` pointer is not such proof.

## Lifetime

The record and its opaque token stay resident until reboot. The first published
provider is fixed. There is no replacement, reissue or token-address reuse.
Revocation is permanent. Queries check revocation and a fresh driver lookup before
dereferencing former provider storage. Console detach keeps its existing refusal.
Reattachment, console replacement/unmap, deactivation, free and a shutdown disable
revoke the matching record before their effects. Warm console mutations serialize
with Giant. The existing cold bootstrap remains serialized; consumers cannot get
a token there.

The record does not add a localcount reference or a hold across a reader barrier.
It is a readiness fact while the caller holds Giant. Revalidate after dropping
Giant. A future lease-bound route transaction must supply its own admission,
consumer lifetime and terminal boot join. This API supplies no route commit,
publication thaw or display/MMIO/firmware authority. Temporary interrupt masking
by the existing console halt path does not remove the installed IRQ handler;
normal `plcomopen` retains responsibility for ordinary tty activation.

## Software checks

```sh
ruby ember/tools/serial-console-ready-contract.rb /absolute/os/source
SKIP_MUTANTS=1 CFLAGS='-O1 -g -fsanitize=address,undefined' \
    ruby ember/tools/serial-console-ready-contract.rb /absolute/os/source
sh ember/tools/cix-plcom-contract.sh
```

The new runner extracts complete actual readiness, common attachment, lifetime
mutation and three frontend function bodies. It uses the actual PLCOM structures,
register definitions and private header. Only unused unchanged callback parameters
receive fixture declaration attributes. Kernel device, tty, interrupt, bus, clock
and ACPI services are controlled; unchanged CIX metadata selection remains in its
separate existing regression.

The positive fixture checks all three actual post-IRQ paths, failed map/hard/soft
interrupts, exact identity/cdev/tty, context and output canaries, revocation and
refusal to reissue. Service traces check attachment order and revoke-before-effects.
Causal controls must compile and then reach a specific CHECK failure. A compile
failure, unrelated crash or timeout is not an accepted control result.

`CC`, `CFLAGS` and one-executable `TEST_RUNNER` support cross compilation and
execution. `COMPILE_ONLY=1` skips execution. `SKIP_MUTANTS=1` runs the positive
contract. `SERIAL_READY_MUTANTS` selects comma-separated controls.
`SERIAL_READY_EVIDENCE` must name a new directory; it records exact source hashes,
extracted inputs, commands, statuses and raw outputs. The runner removes only its
own printed temporary directory. Static analysis, target translation-unit checks
and these service models do not prove a coherent kernel link, MP scheduling,
console-open, a physical serial session or native display lifetime.
