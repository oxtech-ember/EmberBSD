# Linux idr allocation and destruction

The DRM compatibility layer implements Linux `idr` handle tables in
`sys/external/bsd/drm2/linux/linux_idr.c`. `idr_init_base()` recorded the
requested base, but `idr_alloc()` ignored it. Linux never allocates an id below
that base, whatever `start` the caller passes.

DRM GEM, flink names and syncobj handles pass `start = 1`, so they kept the
intended handles. The i915 VM table calls `idr_init_base(..., 1)` and then
`idr_alloc(..., 0, 0, ...)` in `get_ppgtt()` and `i915_gem_vm_create_ioctl()`.
The first VM therefore received id 0, which i915 reserves for "no VM".
NetBSD builds i915 with `CONFIG_DRM_I915_DEBUG_GEM`, so the following
`GEM_BUG_ON(!ret)` panics the kernel:

```
panic: .../i915/gem/i915_gem_context.c:1081: BUG!
i915_gem_context_getparam_ioctl() at netbsd:i915_gem_context_getparam_ioctl+0x7e7
drm_ioctl() at netbsd:drm_ioctl+0x23e
```

Mesa 26 Iris queries `I915_CONTEXT_PARAM_VM` while it creates a context, so
any Iris client reaches this path. It was observed on a Jasper Lake laptop
running the `GENERIC_JSL_TRIAL`-based kernel and Mesa 26.2.4.

`idr_alloc()` now raises `start` to the table base before its range checks.
Tables with base 0 and callers that already pass `start >= base` are unchanged.

## Destruction and resource ownership

`idr_destroy()` releases all IDR metadata, including entries with NULL data.
It does not free objects referenced by the table. The caller must exclude
further use of the IDR and manage those objects separately.
This matches the [Linux IDR contract](https://docs.kernel.org/core-api/idr.html).

The previous implementation destroyed only the lock and leaked remaining nodes.
This matters for the selected legacy PowerVR Services handle backend:
its common cleanup clears successfully released payloads with `idr_replace`,
then destroys the base. The NULL entries still own IDR metadata.
Stopping at a NULL result from `idr_get_next()` would leave those nodes behind.
The native destructor removes each tree node independently of its data value.
The donor handle/resource cleanup must still complete before base destruction.
This change supplies a native prerequisite; it does not implement that provider.

## Regression

Use the selected host prefix through the
[SDK native-check runner](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/native-checks.md).
The contract command is:

```sh
sh ember/tools/linux-idr-contract.sh /absolute/source
```

The contract compiles the complete native `linux_idr.c` with models of the
rbtree, lock, kmem and LWP-specific interfaces. It checks base 1 with start 0,
explicit starts above the base, reuse of a freed id, an empty range at the base,
a negative start, base 0 and a bounded range above base 5.
It checks lock operation pairing and metadata allocation balance.
The allocation-base regression fails its first base-1 allocation without
the base fix. The destruction regression checks a populated table with mixed
NULL/non-NULL data and a table whose payloads were cleared during iteration.
It requires metadata allocations to balance while caller-owned objects remain
unchanged. Without the destruction fix, the first balance check fails.
The models do not execute a kernel, i915 or a DRM client.

The destruction fix passed 193 assertions on the Linux amd64 host with SDK
Clang, both normally and with ASan/UBSan. The unchanged baseline fails the
final regression's first populated-destruction allocation balance check.
The changed source also compiled as an AArch64 kernel object against matching
kernel headers in module mode; that object was not linked or loaded as a module.
Cppcheck and native Clang Static Analyzer introduced no new diagnostics against
the baseline. Existing style/model/header diagnostics remain recorded separately;
these runs are not diagnostic-free. Valgrind was unavailable and was not run.
