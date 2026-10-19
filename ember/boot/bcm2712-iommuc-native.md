# BCM2712 ICAC software attachment

The optional `bcm2712iommuc` ACPI device owns one built-in, boot-lifetime software
object for the cache shared by MMU2, MMU4 and MMU5. This source-only slice provides
attachment, strict lookup, cached status and an explicit one-shot CONTROL read.
Attach, lookup and cached status perform no mapping or MMIO. No cache write,
clock or power operation exists. GPU0 does not own the shared cache.

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
uninitialized core; they are not a successful core status query. If a later
reviewed bridge initializes this same core, the snapshot copies its cached state
under that mutex. No current API, including observation, can initialize it or change its logical leases.
Both APIs reject interrupt context and preserve outputs on error. Outputs must
use valid caller storage. The caller must not free the opaque owner.

The owner and mutex remain built into the kernel for the whole boot. Detach
returns `EBUSY`, including after a retained obligation or fault. There is no
runtime module containing a second owner, mutable core getter, ready flag,
address/callback argument, recovery operation or provider replacement API.
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

## Later access bridge

A future bridge must resolve the platform access lifetime and exclusion of
firmware and all shared-cache writers first. Missing Linux clock/reset/power
properties are not an always-on guarantee. Descriptor validity, `_DEP`, active
clocks, framebuffer absence and previous display samples do not grant access.

The bridge must initialize this actual core once with immutable native callbacks.
A local MMU owner must hold its own lock before the ICAC lock. A complete fixed
sequencer transaction must borrow the core only while the ICAC mutex is held.
The bridge must not return an unlocked mutable core or duplicate it. A bounded
executor must forbid re-entry, pointer retention and callbacks after return.
The final executor and sequencer ownership contract is a separate reviewed step.
No flush, readiness, permission, public mapping primitive or arbitrary transaction
API exists in this patch. The explicit observation does not authorize that bridge. No physical cache access, MMU publication or DMA drain is claimed.

## Source checks

The [native actual-source fixture](../tools/bcm2712-iommuc-native-contract.c)
includes the complete native, pure-core and split query sources. It uses real
ACPICA public types and the existing bounded reference helper. Only kernel
namespace, device, mutex, once, log and fixed bus map/read services are controlled. Private fixture
initialization of the real core tests cached retained/fault state with callbacks
that abort if called. The observation checks exact map geometry/tag/flags, read
offset/count/latch/log order, no automatic I/O, cached errors, one-shot refusal
and mapping retention. Each reset starts a fresh simulated boot; no simulated
unmap/free route exists. This is not hardware execution.

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
