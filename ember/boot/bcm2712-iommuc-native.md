# BCM2712 ICAC software attachment

The optional `bcm2712iommuc` ACPI device owns one built-in, boot-lifetime software
object for the cache shared by MMU2, MMU4 and MMU5. This source-only slice provides
attachment, strict lookup and cached status. It performs no mapping, MMIO, cache
operation, clock operation or power operation. GPU0 does not own the shared cache.

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
under that mutex. No current API can initialize it or change its logical leases.
Both APIs reject interrupt context and preserve outputs on error. Outputs must
use valid caller storage. The caller must not free the opaque owner.

The owner and mutex remain built into the kernel for the whole boot. Detach
returns `EBUSY`, including after a retained obligation or fault. There is no
runtime module containing a second owner, mutable core getter, ready flag,
address/callback argument, recovery operation or provider replacement API.
This slice does not implement suspend/resume or hardware access admission.

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
No observe, flush, map, readiness, permission or arbitrary transaction API exists
in this patch. No physical cache access, MMU publication or DMA drain is claimed.

## Source checks

The [native actual-source fixture](../tools/bcm2712-iommuc-native-contract.c)
includes the complete native, pure-core and split query sources. It uses real
ACPICA public types and the existing bounded reference helper. Only kernel
namespace, device, mutex, once and log services are controlled. Private fixture
initialization of the real core tests cached retained/fault state with callbacks
that abort if called. It is not a hardware backend.

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
