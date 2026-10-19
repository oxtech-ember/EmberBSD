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

This change prepares real reservation snapshots. It does not implement
`DMA_BUF_IOCTL_EXPORT_SYNC_FILE`, `DMA_BUF_IOCTL_IMPORT_SYNC_FILE` or
`DMA_BUF_IOCTL_SYNC`. A driver must publish a real pending fence before
uncertain device access. It must signal that fence only after it proves
completion or isolation.

The native implementation retains the NetBSD Foundation BSD license.
The changes and contract are AI-assisted EmberBSD work. They do not copy
Linux driver implementation code.
