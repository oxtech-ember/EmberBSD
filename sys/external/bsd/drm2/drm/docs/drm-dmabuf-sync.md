# Native DMA-BUF synchronization

The native fence-array callback runs under the child fence lock.
It does not own the array lock. The array uses atomic pending and error
state for callbacks from different children. It publishes the final error
under the array lock before it signals the aggregate. A queued completion
holds one array reference. Other completed callbacks release their references.

An AND array waits for every child. An OR array completes when one child
completes. A later child must not change an already published OR result.
The immutable `dfa_signal_on_any` field identifies these two modes. Code
that flattens an AND aggregate must preserve an OR aggregate as one child.
The constructor accepts the full 64-bit fence context from
`dma_fence_context_alloc()`.

Run the actual-source callback contract:

```sh
DRM_CFLAGS=-DNDEBUG python3 ember/tools/drm-dmabuf-contract.py array
```

The contract uses separate child and array locks. It covers completed
children, asynchronous callbacks, concurrent completion, AND and OR modes,
error propagation, immutable completed results, full contexts, allocation
failure and owned references. Checks remain active with `NDEBUG`.
The host primitives model kernel locks, callback registration and queued
work. This is not a kernel execution or physical DMA test.

## Reservation locking

The common `linux_ww_mutex.c` implementation owns reservation-lock ordering.
`WW_WANTOWN` still belongs to its acquire context; it also records a waiting
caller without a context. A repeated acquisition by that context returns
`-EALREADY`, including after `ww_acquire_done()`. Other contexts retain the
same ticket ordering and `-EDEADLK` behavior as `WW_CTX`.

When a non-context caller causes release without a direct context handoff,
the oldest queued context can acquire the unlocked mutex after waking.
An interruptible waiter accepts an available handoff or acquisition before
returning an interruption. Both paths remove its waiter node exactly once;
only the successful acquisition increments the context's held-lock count.
This does not guarantee starvation freedom under an unbounded stream of
competing callers.

Run the actual-source host regression through the SDK native-check runner:

```sh
"$HOST_PREFIX/bin/python3.15" "$SDK/scripts/run-native-check.py" \
  --host-prefix "$HOST_PREFIX" --compiler-family clang --tool sh \
  --cwd "$SOURCE" --log "$OUTPUT/ww-mutex.log" \
  --bindings "$OUTPUT/ww-mutex-tools" -- \
  "$SOURCE/ember/tools/ww-mutex-contract.sh" "$SOURCE"
```

The runner compiles the real `ww_mutex` implementation and header together
with the real NetBSD red-black tree. A pthread bridge checks mutex ownership
and controls wakeup order and interruption results. The scenarios cover
repeat acquisition, completed acquire contexts, trylock, ticket ordering,
multiple waiters, both context/non-context wakeup orders, interruption before
handoff, successful acquisition despite interruption, and resource cleanup.
A second script argument selects a previous `linux_ww_mutex.c` for the causal
negative control. Each scenario runs in its own bounded host child process.
These checks do not execute the kernel scheduler, LOCKDEBUG, or real kernel
signal delivery and do not establish physical GPU acceptance.

The trylock behavior incorporates the semantic change from NetBSD commit
[`9667e7c425bac450b243ebe79655f73271fc0cea`](https://github.com/NetBSD/src/commit/9667e7c425bac450b243ebe79655f73271fc0cea)
by Taylor R. Campbell, for PR kern/60926. The additional waiter fixes and
regression are AI-assisted EmberBSD work under the retained BSD license.

## Sync-file descriptors

`<sys/dmabuf.h>` defines two native ioctls. Both arguments contain a 32-bit
flags field and a signed 32-bit descriptor field. `READ` requires `FREAD`.
`WRITE` requires `FWRITE`. Zero flags and unknown flag bits fail with `EINVAL`.
An access-right mismatch fails with `EACCES`.

- `DMA_BUF_IOCTL_EXPORT_SYNC_FILE` returns a close-on-exec sync descriptor.
  `READ` snapshots the exclusive writer. `WRITE` and `RW` snapshot the writer
  and all shared readers. The snapshot owns its fences after the reservation
  changes. An empty or fully successful snapshot is already signaled.
- `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` accepts a real native sync-file descriptor.
  `READ` adds a shared dependency. It preserves a newer fence on the same
  timeline. `WRITE` and `RW` combine the old writer with the imported fence.
  `dma_resv_replace_excl_fence()` preserves independent shared dependencies.
  The existing `dma_resv_add_excl_fence()` behavior does not change.

Both operations take the reservation lock. An iterative snapshot flattens
pending AND arrays, keeps OR arrays opaque and visits each node once. It
removes only successful completed dependencies. It preserves negative
completion errors. Pointer deduplication prevents repeated export/import
from constructing an expanding dependency tree.

Failures keep the output descriptor and current reservation dependencies.
An exported descriptor owns one sync-file reference. Close releases it.
The implementation does not add `DMA_BUF_IOCTL_SYNC` or CPU cache maintenance.
Unknown ioctls fail with `ENOTTY`.

A device driver must serialize its submission with the same reservation
lock. It must publish a real pending fence before device access. It must
signal that fence only after it proves completion or isolation. An ioctl
implementation alone does not establish device synchronization.

```sh
DRM_CFLAGS=-DNDEBUG python3 ember/tools/drm-dmabuf-contract.py all
sh ember/tests/build-kernel.sh
```

The sync contract uses the actual ioctl dispatcher, reservation mutation
and native sync-file reference bodies. It reuses the array fixture's host
primitives. It checks ABI numbers, descriptor rights, both fence classes,
OR preservation, errors, repeated import/export, immutable snapshots,
foreign descriptors and allocation/lock failures. The existing mapping
and 80000 concurrent reference-pair controls remain active. The build
wrapper fixture checks dispatch and failed-publication cleanup. None of
these host contracts establishes physical GPU synchronization.

The native implementation retains the NetBSD Foundation BSD license.
The changes and contract are AI-assisted EmberBSD work. They do not copy
Linux driver implementation code.
