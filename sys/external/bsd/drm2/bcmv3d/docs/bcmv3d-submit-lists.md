# Native CL and CSD object lists

Status: source contracts. Physical rendering and Plasma need separate acceptance.

CL and CSD accept from one to 4096 BO handles per submission. The shared
`BCMV3D_RESV_MAX` is a software resource policy. It is not a hardware or UAPI
maximum. It replaces fixed stack arrays in these two native submission paths.
Other header fields and payload validation remain unchanged.

The caller allocates the GEM pointer vector and temporary 32-bit handle vector
before copying handles or taking object references. The count bound and compile
assertions prevent multiplication overflow and a signed count conversion error.
On a 64-bit target, the two vectors use at most 48 KiB per request. This is a
per-request list-storage bound, not an aggregate kernel-memory quota. The
nonblocking allocations can fail with `ENOMEM`. The temporary handle vector is
freed after lookup. The existing device BO storage budget remains independent.

The native `kheapsort` uses one caller-supplied pointer as workspace. It does not
allocate sort storage. GEM and reservation duplicate checks take O(n log n)
work. Integer pointer ordering avoids subtraction and relational comparison of
unrelated C object pointers. A private GEM vector can be reordered before
reservation locking. Every object and reservation must remain alive until the
last unlock. A missing first handle has an empty live prefix. If an earlier
prefix already contains a duplicate GEM object, a later missing handle still
returns `EINVAL`. Other missing handles return `ENOENT`.

The engine borrows the GEM vector only after checking poison under its lock.
PTE mapping, native BO conversion, sync, range checks and busy flags use every
referenced element. Successful completion revokes the mappings before refs are
put. Pre-execution errors release locks, fences, references and metadata through
the original unwind. The vector is freed using its allocation capacity, not the
partial reference count.

An uncertain execution transfers the vector and all live GEM references to the
poisoned job. The vector, mappings, busy flags and BO errors remain owned until
reboot. A later submission must not replace or free that retained vector.
There is no new recovery or hot-removal path. Reservation fence waits keep their
existing common tick budget. Reservation lock acquisition remains interruptible
and is outside that wait budget. GPU phase timeouts are unchanged.

Run these actual-source contracts from the source root:

```sh
ruby ember/tools/bcmv3d-cl-reject-contract.rb
ruby ember/tools/bcmv3d-resv-contract.rb
sh ember/tools/bcm2712-v3d-cl-contract.sh
sh ember/tools/bcm2712-v3d-csd-contract.sh
sh ember/tools/bcm2712-v3d-job-contract.sh
```

The list contract exercises 64, 65, 72, 91, 4096 and 4097 handles, allocation
failures, partial lookup, nonadjacent duplicates, helper errors, full-vector
PTE mapping and revocation, reuse, and retained poison across both engines.
The reservation contract uses the actual core lock helper and native sort.
The TFU contract checks source and destination roles after reservation reordering.
Controlled helper errors prove routing, not native helper reachability or GPU
execution. The PTE checks retain the existing 4 KiB GPU page assumption.
`CL_REJECT_EVIDENCE` and `RESV_EVIDENCE` retain inputs and commands in new
folders. `CC`, `CFLAGS` and `TEST_RUNNER` select compilation and execution.
