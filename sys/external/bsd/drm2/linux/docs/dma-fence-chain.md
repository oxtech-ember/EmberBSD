# Status of a native fence-chain point

`dma_fence_chain_get_payload_status()` is a native DRM/Linux compatibility extension.
It reports completion and error for one selected fence, including nested chain payloads.
PowerVR uses it before executing a job with an external dependency.
The common `dma_fence_get_status()` contract remains unchanged.

The caller holds a reference to a non-NULL fence and holds no fence lock.
The helper borrows that reference. It neither consumes nor returns a reference.
Each chain owns its immutable payload until the chain is released.
The outer reference therefore keeps every visited payload alive.

The helper reads each status from the outer fence toward its payload:

- Return `0` when the current fence is pending, even if its error is already set.
- Return the first negative status from a completed fence.
- Unwrap a successfully completed chain and inspect its payload.
- Return `1` for a successfully completed non-chain fence.

An outer chain error takes precedence over an inner error.
Previous timeline points still gate chain completion. Their errors do not become the selected point's error.
Those points can already have been collected, so traversing history cannot provide a stable error contract.
Other containers, including fence arrays, retain their own status semantics; this helper does not flatten them.
It does not modify any error field or change signaling, callback, or collection rules.
The payload walk is iterative. The underlying signaling probes retain their existing behavior.

## Checks

Run the owner contracts through the
[SDK native-check runner](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/native-checks.md):

```sh
sh ember/tools/dma-fence-chain-contract.sh
sh ember/tools/drm-syncobj-flatten-contract.sh
sh ember/tools/pvr-submit-contract.sh
```

The chain contract compiles the complete production chain implementation and the production status function.
It models kernel locks, generic fence signaling, RCU, and deferred work.
Cases cover pending and completed payloads, nested errors, outer-error precedence, and pending predecessors.
They also cover ignored predecessor errors, retained ownership, and 20,000 completed nested payloads.
Replacing the helper with `dma_fence_get_status()` fails the nested `-EIO` assertion.
The PVR contract compiles the production helper and submit path together.
Nested failed binary and timeline dependencies must fail output fences before VM preparation or GPU execution.

These checks support ASan/UBSan. They do not establish kernel concurrency or hardware acceptance.
See [dma_fence_chain_get_payload_status(9)](../../../../../../share/man/man9/dma_fence_chain_get_payload_status.9)
for the kernel interface.
