# A733 KMS startup and sink retry

An enabled display FDT attachment starts through `config_mountroot`.
No userland stage script is required. An empty `netbsd,observe-only` property
on the display node disables automatic startup. A nonempty value is invalid.
The recovery property follows the existing A733 owner convention.
The explicit stage control still uses the same startup implementation.

The attachment reserves scanout memory and creates one `a733kms` workqueue.
Its dispatch function serves both startup and later atomic commit work.
A mountroot callback only enqueues startup. A softclock callout only enqueues
retry work. Neither callback performs MMIO, DDC, allocation or registration.
The queue IPL and the startup flag mutex cover softclock callers.
Queued, running, registered and stopped state use that mutex.
The separate registration guard excludes concurrent stage requests.
DE/provider and frontend locks are not held across DRM registration.

Startup calls the [DE start contract](a733-de352-native.md#sink-discovery-before-scanout).
The retained HDMI discovery phase checks HPD before DE initialization or DMA.
Only a proved low HPD returns `EAGAIN` and schedules another probe in one second.
Provider, I/O and registration errors remain faults. A registration `EAGAIN`
is a hard failure; it cannot request a new sink-discovery attempt.
An output fault has no implicit reset or resource release.

After scanout admission, the existing DRM registration runs once.
Startup retries then stop. Late timer and mountroot callbacks are no-ops.
The common `drm_kms_helper_poll` owns subsequent connector notifications.
The fixed cached mode, supported pixel format and retained memory guards remain.
Startup does not implement mode renegotiation after a new sink connects.

Run `python3 ember/tools/a733-drm-startup-contract.py` with GCC16.
The fixture executes the production startup, queue dispatch and sysctl bodies.
It checks no-sink retries, sink arrival, stale callbacks after registration,
manual-stage overlap, fault termination and the observe-only opt-out.
The separate DE, HDMI and output contracts check actual discovery and MMIO
boundaries. These host tests do not prove physical display operation.
