# Native fixed-mode BCM2712 KMS

The explicit `WSDISPLAY_BOOTFB_HANDOFF` profile adds one primary plane, CRTC,
TMDS encoder and HDMI-A connector to the native V3D DRM device. The ordinary
render profile keeps its existing feature set. The display uses the same native
GEM objects as V3D. HVS has a separate MMU4 consumer mapping for each frame.

The supported mode is 1920x1080 at 60 Hz with the standard 148.5 MHz timing.
The primary plane accepts only XRGB8888, LINEAR, pitch7680 and offset0.
The plane publishes this explicit XR24/LINEAR pair in `IN_FORMATS`.
The retained DRM core requires `format_mod_supported` to fill the modifier
bitmap. Without that callback, a nonempty blob can contain no usable pairs.
It covers the full CRTC at position0,0. Scaling, rotation, auxiliary planes,
async flips, other timings and foreign GEM objects are rejected.

This source integration is experimental. Source contracts, a linked kernel,
physical KMS flips and a physical accelerated Plasma session are separate gates.
Passing the contracts does not establish the last two gates.

## Registration and hardware lifetime

Mode objects, initial states and vblank storage exist before `drm_dev_register`.
Userspace can observe a disconnected connector before the hardware bind.
Atomic changes are refused until that bind completes.

The ACPI boot continuation binds only its existing admitted native HVS/PV0
owners. CPU console close/drain, firmware acknowledgement, four clock leases,
MMU4 setup and the initial HVS frame precede this bind. The KMS layer cannot
replace any of those conditions with metadata or device lookup.

The initial DRM vblank-off state is installed before bound publication. A route
fault during callback binding prevents publication. Native PV0 remains armed
for HVS retirement even when DRM disables its own vblank accounting.
The DRM counter uses the native accumulated HVS frame epoch. IRQ sequence is a
separate value used to reject stale callbacks; coalesced interrupts do not lose
real frame progression.

HPD is read through the canonical retained HDMI0 map and clock owners. The DRM
poll helper detects connector changes. Persistent HPD IRQ/DDC integration and
additional EDID-selected timings remain separate work. No new mapper or clock
provider is created by connector detection.

## Commit and buffer ownership

Plane-state duplication copies only the generic DRM state. Each successful
`prepare_fb` reserves its own software frame reference. It uses
[`bcmv3d_scanout_reserve`](bcmv3d-scanout.md) without waiting for GPU rendering.
It retains the explicit IN_FENCE separately because the generic helper clears
that field after waiting and does not check its error status.

The commit tail checks that explicit fence and finalizes the native frame.
Finalization waits for the original implicit writer outside owner locks. It
performs consumer PREWRITE only after that writer completes successfully.
Neither preparation nor state copying performs HVS publication.

A disabled CRTC waits for reserved black-list readiness before it captures
the native sequence floor. The condition-variable wait releases the process
owner lock and keeps one tick budget across wakeups. A delayed earlier latch
cannot complete a disable through a floor captured before that wait.

The tail transfers its event pointer under `event_lock` before native submit.
A real latch after the sequence floor captures the current cooked DRM count
and time. It wakes the tail without delivering the event. A delayed older
callback cannot complete this publication. A valid latch callback can still
complete it after a newer non-latch callback updates the count. In that case
the cooked sample is newer; it is not an exact timestamp of the older IRQ.

Successful completion also permits CPU producers to reuse the old buffer.
The tail waits for the old GEM reader fence outside native and reservation
locks. It then confirms full worker reclaim under the process owner lock.
NULL and same-frame transitions do not wait on a current reader.
It detaches event ownership before `commit_hw_done`, cleans the old plane,
and delivers the captured sample only after one last sticky-fault check.
This order covers the frame reference, consumer POST/unload and old-plane
cleanup before userspace can issue its next double-buffer flip.

Post-swap failure sets a sticky KMS error. The detached-event helper completes
`flip_done`, releases its reference and signals OUT_FENCE with a valid negative
BSD errno. It cancels the event without reporting a successful page flip.
This terminal handling does not establish hardware retirement.

Cleanup clears private ownership before one `scanout_put`. A failed put can
already consume that reference and must not be repeated. Native ownership keeps
exposed frames alive until actual later-frame retirement and DMA cleanup.
Unknown hardware state retains its resources until reboot.

A disabled CRTC submits the reserved black list. HDMI timing and clocks continue.
This is logical blanking, not complete HDMI/PHY power-down, suspend or connector
DPMS qualification. Source tests cover rapid reserved-slot readiness, timeout
and fault handling. Physical rapid disable/enable/disable remains a separate
gate. The driver has no hardware detach or fault-reset API.

## Source checks

```sh
ruby ember/tools/bcmv3d-kms-plane-contract.rb
ruby ember/tools/bcmv3d-kms-commit-contract.rb
ruby ember/tools/bcmv3d-kms-event-contract.rb
ruby ember/tools/bootfb-serial-handoff-contract.rb
```

The plane and commit tests extract the actual production functions. They control
DRM, native-owner and kernel services. The plane test also executes the actual
DRM `IN_FORMATS` builder and the production registration with its real function
table, format array and modifier array. It checks the client-visible pair.
The event test also executes the relevant
DRM cancellation, close and state-destruction bodies. Compiled source controls
must reach their expected failed checks. A crash or timeout is not acceptance.
The boot contract checks actual canonical-owner gates and the HPD read boundary.

Each runner accepts its documented evidence, compiler and sanitizer controls.
Use the real GCC16 target context for complete kernel-TU analysis. Host
ASan/UBSan does not execute the kernel. Valgrind requires a supported userspace
platform; it does not instrument native kernel MMIO or DMA.
