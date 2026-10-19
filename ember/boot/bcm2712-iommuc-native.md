# BCM2712 ICAC software attachment

The optional `bcm2712iommuc` ACPI device owns one built-in, boot-lifetime software
object for the cache shared by MMU2, MMU4 and MMU5. This source-only slice provides
attachment, strict lookup, cached status, an explicit one-shot CONTROL read and
a private synchronous MMU4 executor.
Attach, lookup and cached status perform no mapping or MMIO. No default caller
uses the executor. No clock or power operation exists. GPU0 does not own the
shared cache.

## Configuration and identity

The [device selector](../../sys/arch/arm/broadcom/files.bcm2835) permits an explicit
experimental `bcm2712iommuc* at acpi?` configuration line. No ordinary
configuration enables it. The independent descriptor/core objects do not require
a GPU attachment. The GPU-only dependency query has a separate source file;
its public API and validation semantics remain unchanged.

The [native owner](../../sys/arch/arm/broadcom/bcm2712_iommuc_native.c) validates
canonical `\\_SB.ICAC` through the existing [descriptor contract](bcm2712-iommuc-resources.md).
Attach obtains the memory tag only from `aa_memt`. It publishes the owner only
after validation and software construction. It stores an actual zero-initialized
pure core with `initialized=false`. It does not install placeholder callbacks or
pretend that core initialization succeeded.

`bcm2712_iommuc_lookup(cache, out)` validates the canonical descriptor, namespace
node, Device type, `ad_device`, driver identity and the completed owner identity.
Repeated lookup returns the same opaque pointer. A copied descriptor, unrelated
driver, unmatched device or partial attachment is not a provider. `_DEP` only
orders enumeration; it does not require successful dependency attachment.

`bcm2712_iommuc_cached_status(owner, out)` uses the one process-context mutex.
It evaluates no AML. `software_attached` describes software construction only.
`core_initialized=false` and zero core fields honestly describe the current
uninitialized core; they are not a successful core status query. Only the
explicit private executor initializes this same core. Status then copies its
cached state under the mutex. Observation cannot initialize or mutate it.
Both APIs reject interrupt context and preserve outputs on error. Outputs must
use valid caller storage. The caller must not free the opaque owner.

The owner and mutex remain built into the kernel for the whole boot. Detach
returns `EBUSY`, including after a retained obligation or fault. There is no
runtime module containing a second owner, mutable core getter, ready flag,
public address/callback argument, recovery operation or provider replacement API.
This slice does not implement suspend/resume or hardware access admission.

## Explicit CONTROL observation

`bcm2712_iommuc_observe_control(owner)` is an experimental, process-context,
one-shot operation. Its external prerequisites are a normal-active BCM2712
interconnect lifetime and exclusion of firmware and all MMU2/MMU4/MMU5/cache
writers. The provider mutex serializes only cooperating native callers. No caller
property, descriptor, `_DEP`, clock lease or earlier sample proves those external
conditions. This API does not establish general platform power ownership.

The operation latches its attempt before mapping the fixed ICAC address/length
with `aa_memt` and flags0. After successful mapping, it records the read attempt
and logs the native bus stall/abort boundary before exactly one `read_4` at
CONTROL+0. It caches the returned value and completion separately. The fixed
known fields are ENABLE bit0, FLUSH bit1 and FLUSHING bit2. Values0..3 are accepted
samples, not idle or mutation permission. Unknown bits return `EOPNOTSUPP`;
FLUSHING returns `EBUSY`. Both keep a valid raw sample and the first error.

A map error is cached with no read attempt or valid raw value. Every operational
attempt, including map failure, consumes this one-shot. Repetition returns the
original error, or `EALREADY` after an accepted sample, with no new mapping/read.
Invalid owner/context or a future already-initialized core refuses before that
attempt. Status reports exact started/mapped/stage/read-count/raw-valid facts.
It leaves the actual core uninitialized with zero core counts.

The successful mapping remains in the boot-lifetime owner on every outcome.
There is no unmap, detach, retry, reset, disable or cleanup operation. A native
bus stall or abort may never return; a finite call count is not a timer bound or
recoverable errno. A returned read proves only that transaction at that time.
The output API is cached status; its NULL/foreign/interrupt errors preserve the
caller's output. The added status fields require matching kernel/client headers.
No physical cache observation is claimed by these source checks.

## Private synchronous MMU4 executor

The [private executor header](../../sys/arch/arm/broadcom/bcm2712_iommuc_executor.h)
provides `bcm2712_iommuc_execute_mmu4(owner, operation, arg)`. This is trusted
built-in kernel composition. The runtime callback check is `NULL` only; no
whitelist, identity token or client registration is implied. There is no default
caller or native MMU4 programming adapter in this slice.

The caller establishes accessibility and excludes firmware and all shared-cache
writers. Missing Linux clock/reset/power properties are not an always-on grant.
A completed CONTROL observation is a recorded prefix only. It does not establish
these external conditions. The caller resolves ICAC before the transaction and
holds its local MMU4 lock before the common ICAC mutex. Preallocate backing,
plans, maps and native references outside that common mutex. Seal backing before
sequencer entry. Do not perform AML, allocation or clock operations inside it.

The provider sets its boot-lifetime MMU4 attempt before core initialization or
the callback. Even a clean pre-retain error with a released reservation consumes
that attempt. A new callback/argument cannot retry it. After entry validation,
repeat returns the cached first error, or `EALREADY` after success, before gates
changed by the previous attempt. Invalid context/owner/NULL callback does not
consume the attempt. Cached attempted, first-error and fault fields are separate.
Clean error can have attempted=true and fault0.

The operation uses a local actual MMU4 sequencer under the ICAC mutex. Its
`init -> program -> status` and permitted untouched `fini` all complete before
return. The init stores a cache pointer, so the local object must not escape.
Copy its status before `fini`. Preserve the primary error and separate close
error in the persistent native MMU4 owner. Never close after retention, mutation,
fault or unknown status. That native owner retains backing, maps, render/tag
references and callback storage/text. The shared MMU4 lease remains in this
provider after the local sequencer disappears. The provider does not save the
operation or argument. No asynchronous callback path exists in the sequencer.

This explicit entry initializes one actual cache core with immutable native
callbacks. They use its existing `aa_memt` mapping, read only CONTROL+0, accept
only write3 at CONTROL+0 and issue a four-byte READ|WRITE bus barrier. The core
retains its fixed preflight/write/1024-poll protocol. A valid-owner/locked native
protocol rejection latches the first fault. Native and actual core faults block
later callbacks before MMIO, even if the operation ignores an error. Bus stalls
and aborts remain unrecoverable; void native access returns success only after
the actual access/barrier returns.

The operation must not re-enter a provider API, export a core pointer, replace
its callbacks, close/re-initialize the shared core or change MMU2/5 leases.
Re-entry returns `EDEADLK` before AML or mutex acquisition. Return checks detect
callback/initialization and unrelated-lease changes. An operation error with a
remaining reservation/retention also faults the native provider. These checks
cannot undo trusted C misuse or sandbox it. Any uncertainty retains remaining
objects; no retry, disable, write0, restore, unmap or detach follows. This is not
hardware access admission, physical cache acceptance, MMU publication or DMA drain.

## Source checks

The [native actual-source fixture](../tools/bcm2712-iommuc-native-contract.c)
includes the complete native, cache/MMU4 core and split query sources. It uses
real ACPICA public types and the existing bounded reference helper. Kernel
namespace, device, mutex, once, log and native bus services are controlled. A
[separate test bridge](../tools/bcm2712-iommuc-executor-backing.c) reuses the full
existing backing fixture with actual backing, frame-helper and table-encoder
translation units. Only its kernel allocation/BO/DMA/pmap services are doubled.
The real prepare path validates, fills and PREWRITEs preallocated storage under
the controlled ICAC lock. It does not allocate. The fixture instruments the
cache-init call before invoking unchanged `actual_cache_init`. It separately
injects one synthetic init error to test attempt consumption. This is not a
firmware failure or a changed core algorithm. The local native owner lock does
not exist in this slice. The fixture supplies `local_locked=true` as a controlled
caller precondition; it does not prove a future native owner lock implementation. Other scoped local-IO failure cases
control the prepare boundary; they do not replace the successful actual path.
Private fixture initialization also tests cached state without callbacks. The observation checks exact map geometry/tag/flags, read
offset/count/latch/log order, no automatic I/O, cached errors, one-shot refusal
and mapping retention. Each reset starts a fresh simulated boot; no simulated
unmap/free route exists for ICAC. Sealed backing cases run in separate simulated
boot processes; their storage remains retained until process exit. This is not
hardware execution. Executor cases check no pointer escape, pre-close snapshots,
separate primary/close errors, provider attempts across fresh argument objects,
first-fault callback suppression, unrelated leases and exact native write/barrier.
Mutants must compile and fail by the fixture CHECK with SIGABRT, not an arbitrary
nonzero status.

```sh
sh ember/tools/bcm2712-iommuc-native-contract.sh /absolute/os/source
```

`ACPICA_INCLUDE` selects matching ACPICA source headers. `CC`, `CFLAGS` and
`TEST_RUNNER` support cross compilation and one executable wrapper for both
positive and mutant runs. `ICAC_NATIVE_COMPILE_ONLY=1` compiles without execution.
`ICAC_NATIVE_SKIP_MUTANTS=1` runs the positive. `ICAC_NATIVE_MUTANTS` selects
focused compiled causal cases. `ICAC_NATIVE_OUTPUT` saves the executable;
`ICAC_NATIVE_EVIDENCE` saves exact inputs, source hashes and dependency files.
The existing descriptor/dependency fixture still runs, with unchanged query
bodies apart from a private context-helper name after the translation-unit split.
These checks do not replace a coherent kernel link or physical acceptance.
