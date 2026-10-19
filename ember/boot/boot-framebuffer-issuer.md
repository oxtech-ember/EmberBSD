# Cold boot framebuffer defer issuer

`WSDISPLAY_BOOTFB_DEFER` is default-off. It selects
`WSDISPLAY_EARLYFB_HOOKS`. No default configuration or native display caller
selects it. It can defer one actual cold simplefb fallback and issue one opaque
lease. It does not close console output, drain writers, install a replacement
console, issue a firmware request or grant MMIO, DMA or display ownership.

## Association

The shared [FDT reader](../../sys/dev/fdt/simplefb_subr.c) validates one complete
reg0 with address and size cell counts of one or two. It rejects malformed
lengths before `fdtbus_get_reg64`. It checks the full uint64 range, exact native
address/size conversions, page-rounded end, six supported formats and signed
genfb geometry bounds before it commits output. It never validates a truncated
`fdtbus_get_reg` result. The generic
[range conversion contract](fdt-register-ranges.md) is a separate check.

The actual ARM preattach uses this reader before its map. For this native
association it accepts only a direct `/chosen` child, empty `/chosen/ranges`
and a root parent. This matches the EFI GOP node shape. Other translation
relations are unsupported. The known generic ARM map method must be selected
before the actual map. The static capture contains the actual FDT blob/node,
full aperture, geometry/format, map method/flags/handle and actual MI type/cookie.
A release-store publishes capture only after the real MI preattach and complete
synchronous replay succeed. Physical address zero and a successful handle zero
are valid. Map, binding or replay failure creates no capture.

The actual selected cold simplefb attachment checks that domain before range
decoding. Its ARM bridge rechecks the exact blob, node, stdout selection,
complete aperture and all descriptor fields. A different node with the same
numeric address is insufficient. A copied generic tag is accepted by its known
function, not by tag-pointer equality. The known nonposted wrapper is accepted
only while its generic target is also known. Unknown or changed methods refuse.
A nonzero MD address override remains supported by ordinary genfb. If it differs
from the actual early mapping, the native association refuses it.

MI stores only its actual binding and the fallback record. It includes no ARM
or FDT header. Unsupported MD selection has no weak or ready-success stub.

## Fallback and lease

One MI record replaces the former simplefb deferred/owner/committed globals.
The built-in frontend calls the real `config_finalize_register` under Giant.
REGISTERING precedes the call. DEFERRED follows only if the finalizer has not
already run. The real register can execute fallback immediately and return zero;
that result alone does not create a lease window. Registration failure closes
the window before the direct built-in fallback.

`simplefb_console_claim(&lease, &descriptor)` requires process context, the
native DEFERRED record, exact association and current actual early READY binding.
It takes Giant. A claim may occur after `cold` clears only for a previously
created cold window. Errors preserve both disjoint caller outputs. There is one
lease issuance per record. Cancellation does not permit a stale token to cancel
a newly issued lease.

A CLAIMED finalizer records pending fallback and returns zero. It keeps the
record after the actual finalize hook is freed. `simplefb_console_cancel(lease)`
is valid only before any hardware handoff. It rechecks association/binding and
runs one pending built-in fallback. Without pending work, the existing finalizer
will run that fallback later. Unknown association/binding moves to RETAINED;
it does not attach genfb. The callback and device must remain resident.
FALLBACK_STARTING precedes the first selected genfb effect, including a failed
attach. Late registration cannot reopen a finished window.

The VirtGPU `device_t` reserve/query/release/commit APIs adapt to this same record.
They keep their existing unreadable-descriptor reservation policy. Legacy owners
and native leases cannot convert into each other. They exclude each other.
The issuer does not reorder render or fallback finalizers. A future cold caller
must claim before fallback. It cannot recover a lease after genfb publication.

## Source checks

```sh
ruby ember/tools/bootfb-issuer-contract.rb /absolute/os/source
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    BOOTFB_SKIP_MUTANTS=1 ruby ember/tools/bootfb-issuer-contract.rb /absolute/os/source
```

The runner compiles the unmodified parser as a separate C translation unit and
includes the unmodified MI issuer C and real private headers. It extracts complete
unchanged ARM, simplefb, wsdisplay, replay and configuration functions/state with
their notices. It reuses the existing reader service harness. FDT, map, rasops,
device, allocation, Giant and configuration-wait services are controlled.
The real registration/finalizer code covers immediate, queued, claimed and legacy
paths. Its private pseudo-device service table is a null sentinel. This fixture
invariant does not validate the full kernel pseudo-device table. Tests cover full-width overflow/schema/page bounds, PA0/handle0, source and
method changes, exact tuple matching, output preservation and one fallback.
Compiled causal mutants must reach fatal CHECK, including under NDEBUG.

`CC`, `CFLAGS` and one-executable `TEST_RUNNER` select cross compilation/execution.
`BOOTFB_COMPILE_ONLY=1` compiles without running. `BOOTFB_SKIP_MUTANTS=1` selects
positives. `BOOTFB_MUTANTS` selects named controls. `BOOTFB_EVIDENCE` must name a
new directory; it saves input hashes, extracted inputs, argv and raw outputs.
The existing `simplefb-contract.rb` routes current-source legacy checks through
this shared-record fixture. Its old-export baseline controls remain available.

These controls establish no real MP race or grace-period behavior. Issuer claim
and cancel leave actual cnputc and replay enabled. Publication interlocks,
close/drain, an independent sink and irreversible hardware handoff remain
separate work. Native TU checks require actual selected warning flags, matching
headers and explicitly generated off/on option headers. They do not establish a
coherent kernel link or physical display handoff.
