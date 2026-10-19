# Cold boot framebuffer defer issuer

`WSDISPLAY_BOOTFB_DEFER` is default-off. It selects
`WSDISPLAY_EARLYFB_HOOKS`. No default configuration or native display caller
selects it. It can defer one actual cold simplefb fallback and issue one opaque
lease. It does not close console output, drain writers, install a replacement
console, issue a firmware request or grant MMIO, DMA or display ownership.

## Loader source facts

The optional `WSDISPLAY_BOOTFB_HANDOFF` loader path writes a source record.
It does not select or change `WSDISPLAY_BOOTFB_DEFER`. No default loader Makefile
selects it. The [shared decoder](../../sys/dev/fdt/bootfb_source.h) has no issuer,
provider, close, capture, MMIO or display permission API.

`/chosen/ember,bootfb-source-v1` is exactly 32 bytes in big-endian order:

| Offset | Width | Value |
| --- | --- | --- |
| 0 | 4 | Version 1 |
| 4 | 4 | Producer: unknown 0, selected EFI ACPI64 1 |
| 8 | 4 | Unclassified 0, present 1, unsupported 2, absent 3 |
| 12 | 4 | Reserved zero |
| 16 | 8 | Exact EFI GOP enumeration status |
| 24 | 8 | Exact handle count returned by `LibLocateHandle` |

Only the actual 64-bit ACPI tree constructor establishes producer 1. An external
DTB selection or any nonempty overlay-file attempt makes the source unknown,
including a failed load or apply. Compatible properties alone do not establish
origin. GOP classification occurs before ExitBootServices. Physical address zero
is present. Unsupported formats, protocol errors, EFI warnings and errors other
than exact EFI_NOT_FOUND with count zero do not establish absence.

The actual checked cleanup reports memory-map, ExitBootServices, runtime-call and
runtime-property results. The existing void cleanup and runtime wrappers remain.
After that real chain, the producer validates the complete tree, neutralizes any
copied fixed-size source record and commits its new record. It uses the existing
writable FDT capacity. It makes no new EFI allocation after ExitBootServices.
The existing checked pack still runs. The final packed query follows it. A failed
map, EBS, runtime call or relevant FDT write cannot create an accepted absent
record. A framebuffer compatible anywhere, including a disabled node outside
`/chosen`, prevents that classification. The strict query rejects malformed or
duplicate records, invalid strings, ambiguous chosen nodes and incomplete v17
structure. It requires an empty root name and one exact `chosen` node. It rejects
`chosen@*` siblings and properties after child nodes. It uses caller-supplied
readable capacity, not encoded totalsize alone.
These are source facts. They do not prove native ownership or access readiness.

```sh
ruby ember/tools/bootfb-source-contract.rb /absolute/os/source
ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    BOOTFB_SOURCE_SKIP_MUTANTS=1 ruby ember/tools/bootfb-source-contract.rb /absolute/os/source
```

The runner composes complete actual EFI producer, GOP, prepare, cleanup and
runtime functions with the real libfdt. EFI types, storage, allocation and
unrelated prepare services are controlled. FDT failures wrap real operations;
they do not replace the producer. Extracted functions retain source paths,
lines and notices. These host services do not validate the native EFI ABI.
Tests cover wire bytes, full-tree errors, status/count/format/PA0, source history,
cleanup errors and record-after-runtime ordering. Each causal control must compile
and reach a fatal CHECK. A crash, compile failure or timeout is not a rejection.
`CC`, `CFLAGS`, `TEST_RUNNER`, `TMPDIR`, `COMPILE_ONLY=1`,
`BOOTFB_SOURCE_SKIP_MUTANTS=1`, `BOOTFB_SOURCE_MUTANTS` and a new
`BOOTFB_SOURCE_EVIDENCE` directory select tools and evidence. Native checks must
also compile both complete loader TUs with actual off/on Makefile flags.
No absence admission, coherent loader link or physical result is claimed by
this source contract. Valgrind is unsupported on the Darwin host;
the executable fixture uses ASan/UBSan instead.

## ARM cold source capture

With `WSDISPLAY_BOOTFB_HANDOFF`, actual ARM FDT device registration calls
`arm_simplefb_source_capture` before simplefb preattach. It passes the real
512 KiB FDT array and its capacity. The first call consumes the only attempt.
Capture requires cold process context under Giant, without panic or interrupt.
A failed context, pointer, structure or source check leaves typed UNKNOWN.
Later calls cannot retry or replace that result.

The private record accepts only the strict absent EFI ACPI64 source with no
framebuffer-compatible node. It stores the actual blob, capacity, validated
length, chosen offset, typed source fields and SHA256. A private revalidation
repeats identity, full-tree, record and digest checks before capture commits.
It detects later changes without rewriting the original facts. There is no
public record getter, admission token or new display policy.

The same source runner includes complete actual capture and registration
bodies, the real FDT array declaration and the actual SHA2 implementation.
It checks call order, full capacity, one attempt, context failures, malformed
trees and changes to identity, length and same-length bytes. Its controlled
kernel services do not establish an MP race or native ABI. Native validation
requires both complete ARM TUs with matching generated headers and explicit
off/on options. No coherent kernel or physical handoff result is claimed.

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

## Native claim-only job

With both `BCM2712_V3D_RENDER` and `WSDISPLAY_BOOTFB_DEFER`, the validated cold
BCM2712 ACPI attachment queues one claim-only `config_interrupts` job after its
renderer finalizer is registered. It retains the real lease and descriptor in
its boot-lifetime softc. The real pending join completes the job before either
simplefb or renderer finalizer runs. The job does not wait for a finalizer.

The sole private renderer attach interface passes the same borrowed opaque owner
as its fifth argument, after device, bus-space tag, DMA tag and ACPI handle.
The actual renderer stores that pointer. MI/FDT fields stay in the native owner;
there is no raw getter or permission flag. All callers need a coherent rebuild.
A null owner or a refused claim grants no headless or display access.

Read-only observation failure cancels the lease. A takeover error retains it,
because the error does not identify pre-seal versus uncertain writes. The current
renderer setup error paths cancel only after their existing cleanup returns.
This is certified against its setup-only call graph: no MODESET, display or MMU4
operation occurs there. Adding such a call invalidates this cancellation rule.
Cancellation failure or unknown binding retains the owner and records the error.
Successful render-only attachment also cancels the live lease. The renderer keeps
only the same resident opaque record, now CANCELED with no lease. It receives no
display authority. Both finalizer orders must attach the deferred genfb console
exactly once before init opens `/dev/console`. A retained early console without a
real tty leaves `cn_dev` at NODEV and triggers `cnopen: no console device`.
A future native display caller must provide a valid console-open contract and a
separate handoff lifetime. No framebuffer guard is bypassed.

This job adds no close/drain, sink, firmware request or display attempt. Existing
console output/replay stay active. Neither a claim nor the stored pointer grants
MMIO, DMA or a later handoff. The options remain default-off.

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
and cancel leave actual cnputc and replay enabled. The separate default-off
[checked publication foundation](console-publication.md) covers selected binders.
Close/drain, an independent sink and irreversible hardware handoff remain
separate work. Native TU checks require actual selected warning flags, matching
headers and explicitly generated off/on option headers. They do not establish a
coherent kernel link or physical display handoff.

The focused job contract is separate:

```sh
ruby ember/tools/bootfb-claim-job-contract.rb /absolute/os/source
```

It reuses the actual issuer/reader fixture and compiles complete unchanged
configintr/pending/finalize, ACPI attach/job/finalize and renderer attach bodies.
Only ACPI, scheduler, V3D engine, DRM and other native services are controlled.
The real renderer argument/store and each setup-error unwind are exercised.
The controlled transition to a warm immediate callback still calls the actual
`config_interrupts` body. No real scheduler race is established. Fake renderer
services are disposed only between independent simulated boots; this is not a
native detach or release protocol.

`CLAIM_JOB_SKIP_MUTANTS=1` selects positives. `CLAIM_JOB_MUTANTS` selects named
controls. `CLAIM_JOB_SKIP_POSITIVES=1` omits positives for a scoped control run.
`CLAIM_JOB_COMPILE_ONLY=1` supports target compilation without execution.
`CC`, `CFLAGS`, `TEST_RUNNER` and `TMPDIR` select tools and work storage.
`CLAIM_JOB_EVIDENCE` must name a new directory. It preserves source hashes,
extracted inputs, exact argv and raw outputs. A control must compile and reach
its expected fatal CHECK; an unrelated abort or a timeout does not pass.
