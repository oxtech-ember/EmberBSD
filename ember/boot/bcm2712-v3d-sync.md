# Native V3D binary synchronization

## Design and acceptance plan

The accepted CM5 QPU path retires work synchronously, after cache cleaning,
PTE revocation and DMA ownership transfer. Mesa also needs DRM binary syncobj
inputs and outputs. Reuse the existing DRM syncobj and native dma_fence APIs.

Global constraints: retain the bounded synchronous queue, poison retention and
ordinary-kernel recovery path. Do not advertise timelines, general CL, PRIME,
KMS or complete Mesa support. Cross-build from recorded EmberBSD sources.
Keep hardware evidence separate from host contracts. No new Python tooling.

1. Repair shared DRM wait progress first. Exercise the actual wait function:
   partial WAIT_ALL completion must still time out; publication immediately
   before sleeping must not lose its wakeup; allocation failures must destroy
   local wait resources. Observe failure before fixing and passing afterward.
2. Add binary input/output fences to native TFU/CSD. Snapshot the input before
   replacing output (Mesa uses the same handle). Wait outside the engine lock;
   preserve output on validation/dependency failure. Publish only admitted
   work and signal after retirement, or with an error on poison. Test reference
   ownership, allocation failure, close races, timeout and inter-engine chains.
3. Cross-build the render kernel and a static acceptance program. On CM5 test
   create/reset/signal/wait, finite WAIT_ALL, same-handle CSD chains, TFU/CSD
   alternation and retained handles. Restore and verify the ordinary kernel.
   Record hashes and boundaries before changing Mesa capabilities.

Review focus: missed wakeups; lock ordering; input/output aliasing; fence and
syncobj lifetime; premature success after IRQ; errors that release GPU-owned
storage. The implementation and acceptance record will replace this plan as
each stage completes. Existing CSD evidence is in [the CSD record](bcm2712-v3d-csd.md).
