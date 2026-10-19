# A733 native DRM frame lifetime

The native frame bridge connects a GEM CMA object to the
[DE352 repeated-frame provider](a733-de352-native.md#repeated-linear-frames).
It validates the map and protects its storage during scanout.
It does not register a DRM device, CRTC, plane or connector.
The KMS frontend and physical page-flip acceptance remain separate work.

## Admission

`sun60i_a733_drm_frame_submit` runs in a sleepable worker.
The caller owns a GEM reference from this CMA driver.
The caller holds no DRM, reservation or DE provider lock.
The object must use the exact restricted DMA tag from the cached DE state.
The map must contain one loaded segment below 4 GiB.
The complete padded image must fit the object, map and segment.

The profile accepts linear XRGB8888 at offset zero.
It requires the exact admitted width and height.
The pitch must cover all pixels and have 64-byte alignment.
The bridge rejects scaling, foreign tags and out-of-range DMA spans.
It leaves the completion output unchanged on failure.

## Synchronization and ownership

A producer wait alone does not exclude a later writer.
The bridge locks the reservation and examines its current exclusive fence.
If that writer is pending, the bridge unlocks and waits with a timeout.
It then locks and checks the current writer again.
This also checks a writer that replaced the previous fence during the wait.

After the writer succeeds, the bridge reserves a shared slot and adds a
pending READ scanout fence under the same reservation lock.
It unlocks before it calls the DE provider.
The bridge never holds a reservation lock during display work.
Producer errors, timeout and repeated writer replacement stop submission.

The DE retain callback takes a fence reference before its first pointer attempt.
The release callback runs only after proven retirement or a pre-DMA abort.
It signals the READ fence and drops the GEM reference outside the fence lock.
This breaks the GEM-to-reservation-to-fence-to-GEM reference cycle.
The reservation can retain a signaled fence without retaining its own object.

An uncertain pointer attempt keeps the READ fence pending.
It keeps both the frame storage and callback reference until reboot.
A failed KMS commit must not release this retained scanout reference.
Its separate completion fence can report an error.

The bridge checks that a successful provider call actually retained the frame.
It also accepts proven retirement before the submit caller resumes.
It does not require the frame to remain active after that success.

## Source checks

Run the actual-source host contract from the repository root:

```sh
python3 -B ember/tools/a733-drm-frame-contract.py
```

The host needs Python 3 and a C11 compiler.
Use `CC`, `A733_DRM_CFLAGS` and `TEST_RUNNER` for a supported analysis runtime.
The contract keeps its checks active when `NDEBUG` is defined.
It covers invalid maps, reservation failures, writer replacement, frame
retention, early retirement, post-pointer failure and reference-cycle removal.
The contract uses host models for locks, fences, GEM objects and DMA maps.
It does not execute a kernel driver or access display hardware.

Target GCC16 compilation uses the public DE provider header and kernel flags.
Cppcheck, GCC fanalyzer and Clang Static Analyzer supplement the host contract.
These checks do not establish DRM device registration, physical page flips,
continuous vblank counting or an HDMI desktop.
