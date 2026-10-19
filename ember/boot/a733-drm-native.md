# A733 native KMS and frame lifetime

The native frame bridge connects a GEM CMA object to the
[DE352 repeated-frame provider](a733-de352-native.md#repeated-linear-frames).
It validates the map and protects its storage during scanout.
The fixed-mode KMS frontend uses this bridge for each submitted frame.
Physical KMS page-flip acceptance remains a separate hardware check.

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
It signals the READ fence under the fence lock.
It drops the GEM reference after it releases that lock.
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

## Fixed-mode KMS frontend

`sun60idrm` registers one primary plane, CRTC, TMDS encoder and HDMI connector.
The native DE provider owns the display clocks, memory admission and output.
The KMS frontend calls the existing repeated-frame interface.
It does not program display registers.

The FDT node uses `compatible = "allwinner,sun60i-a733-display"`.
Its `display-engine` property names the native DE352 node.
It has no clock, reset, power-domain or MMIO property.
`GENERIC64` selects the separate `sun60idrm` attachment.
Attach creates controls but does not register DRM or start display output.

First, complete the native DE stage from [the DE contract](a733-de352-native.md).
Then set `hw.sun60idrm0.stage=1` to register the KMS device.
The frontend accepts only an admitted DE state and its exact restricted DMA tag.
A missing or not-ready provider returns an error without starting registration.
A failure after client acquisition is sticky.
`hw.sun60idrm0.fault` returns the first registration or commit error.
There is no detach, restart or display-stop contract in this profile.

The connector advertises one exact cached EDID mode.
The frontend accepts linear XRGB8888 at offset zero.
It rejects scaling, rotation, alternate modes and noncontiguous imports.
It rejects atomic disable, inactive CRTCs and DPMS off.
These operations cannot claim to stop the native DE scanout.

The commit path admits one job through its complete cleanup.
It queues a sleepable worker for blocking and nonblocking commits.
The worker holds no modeset, reservation or provider lock.
It waits for an explicit producer fence for at most one second.
The frame bridge then checks the current implicit writer under its reservation lock.
It installs the separate pending READ fence before native submission.

A page-flip event is sent only after successful native submission.
Its timestamp is the provider's second observed output event.
Its sequence is zero because the provider has no continuous hardware frame count.
`WAIT_VBLANK` and CRTC sequence operations remain unsupported.
An observed software event sequence is not exposed as a hardware counter.

If a job fails after software state swap, the output fence reports the error.
The frontend cancels the successful page-flip event and completes helper bookkeeping.
It records the first fault and rejects later commits.
It does not signal a retained scanout READ fence or release a possibly active frame.

The native dumb allocator checks dimensions and uses a 64-byte pitch.
It drops its temporary GEM reference exactly once after handle creation.
Its native PRIME mmap callback uses byte offsets relative to the authorized object.
It rejects private, executable, unaligned and out-of-range mappings.

Run the additional actual-source host contracts from the repository root:

```sh
python3 -B ember/tools/a733-drm-admission-contract.py
python3 -B ember/tools/a733-drm-buffer-contract.py
python3 -B ember/tools/a733-drm-event-contract.py
python3 -B ember/tools/a733-drm-register-contract.py
python3 -B ember/tools/a733-drm-stage-contract.py
python3 -B ember/tools/a733-drm-worker-contract.py
```

These contracts model the external DRM, DMA and scheduling interfaces.
They check the actual admission, cleanup, event and worker functions.
They check stage retries, sticky failures and a nested registration request.
They also release the caller's atomic-state reference before the worker runs.
They keep assertions active with `NDEBUG`.
They do not execute the DRM core or establish physical HDMI page flips.
Physical KMS and desktop acceptance requires a matched kernel and native DE stage.
