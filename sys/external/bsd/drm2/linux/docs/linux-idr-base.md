# Linux idr allocation base

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

## Regression

```sh
sh ember/tools/linux-idr-contract.sh /absolute/source
```

The contract compiles the complete native `linux_idr.c` with models of the
rbtree, lock, kmem and LWP-specific interfaces. It checks base 1 with start 0,
explicit starts above the base, reuse of a freed id, an empty range at the base,
a negative start, base 0 and a bounded range above base 5. It also checks that
the tree is only accessed under the idr lock and that no node leaks.
The unmodified source fails its first base-1 allocation; the fixed source
passes. The models do not execute a kernel, i915 or a DRM client.
