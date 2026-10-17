# Native DRM change events

The native `drm_sysfs.c` adapter publishes an advisory device-monitor event.
Both `drm_sysfs_hotplug_event()` and `drm_sysfs_connector_status_event()` use it.
Only a registered DRM device with a primary minor and native parent can publish.
The connector property argument does not narrow the notification.
Consumers must query current connector state after each event.

## Event contract

| Field | Value |
|---|---|
| event | `device-change` |
| device | `dri/card` followed by the primary minor index |
| parent | The real autoconfiguration device name |

The primary and render allocators have independent indices.
The sender must never derive a card index from a render index.
The dictionary owns copies of both names before publication.
No display state, address, EDID or application data enters this event.

The sender uses `devmon_insert_vec`, not a direct optional `drvctl` reference.
A zero return transfers dictionary ownership, including when no listener exists.
A nonzero return leaves ownership with the sender, which releases the dictionary.
Allocation failures discard this advisory event without altering DRM state.
The bounded native monitor queue can also discard events.
Applications must enumerate initial state and must not count events as state transitions.

The existing native device-monitor protocol remains unchanged.
The Ports `ndevd` transport retains its three 65-byte fields.
A compatible libudev consumer must map this event to a DRM card `change` action.
It must validate the pathname and identify the node as `drm_minor`.
The stock device-attach MAKEDEV hook does not create nodes for a change event.

## Caller lifetime and context

Callers must retain the device and connector throughout the call.
They must stop notification work before unregister and teardown.
The registration check is not a reference acquisition or a lifetime lock.

The sender can sleep and requires process context.
An IRQ handler must defer notification to a process-context worker.
Hard and soft interrupt calls return before allocation; diagnostic kernels also assert.
The sender does not acquire modeset locks or call a display driver.
Callers must still obey the locking contract of their specific DRM helper.
For example, `drm_kms_helper_hotplug_event()` requires that callers release modeset locks.
This is not a universal restriction on every direct native adapter caller.

## Checks and limits

```sh
ruby ember/tools/drm-hotplug-contract.rb
CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    ruby ember/tools/drm-hotplug-contract.rb
sh ember/tests/build-kernel.sh
```

The host test extracts the real sender and both public entry points.
It tests registered and absent devices, independent minor indices, copied names,
allocation failures, queue ownership, no listeners, and interrupt rejection.
Four compiled mutations must fail: missing registration guard, wrong minor index,
reversed ownership, and missing interrupt guard.
The original empty adapter fails the same event-delivery test.
Testing that baseline requires the explicit `--expect-baseline-failure` option.
Without this option, a missing sender is an error.
The build test checks dispatch and error propagation with mocked compiler tools.

GCC16 target object compilation and `-fanalyzer` pass with real AArch64 headers.
Clang Static Analyzer and Cppcheck also parse the full target translation unit.
Their retained header findings are separate from the changed sender.
Host ASan/UBSan checks pass; Darwin does not support Valgrind Memcheck here.
None of these checks executes the native monitor queue or a physical display.

This change supplies event transport only.
It does not implement HPD interrupts, DDC/EDID reads, KMS, mode changes, or scanout.
A firmware framebuffer descriptor does not establish current connection state.
End-to-end acceptance needs a native display driver and a compatible userspace consumer.
