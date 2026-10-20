# Shared GEM framebuffer allocation

The native [GEM framebuffer helper](../../sys/external/bsd/drm2/drm/drm_gem_framebuffer_helper.c)
uses the common slab allocator. Allocation must clear unused GEM plane pointers.
The format-fill helper does not initialize them. `drm_gem_fb_destroy` visits
all four slots, so uninitialized slots can become invalid object releases.

The error path must also use `kfree`. Native `kmalloc`/`kzalloc` return a pointer
after a `linux_malloc` prefix. Passing that payload directly to `kmem_free`
uses the wrong pointer and size. The helper now uses `kzalloc` and the matching
`kfree` on every allocation exit. Object references acquired before a later
lookup, size or framebuffer-init failure are released first.

Run the actual-source lifetime contract:

```sh
ruby ember/tools/drm-gem-fb-contract.rb
```

The fixture executes the complete framebuffer helper, the actual format-fill
function, the actual slab allocation/free bodies and their prefix structure.
DRM object and kmem services are controlled. Poisoned nonzero allocation bytes
expose the unused-plane error. Pointer/size checks expose the allocator error.
Both regressions fail against the earlier source. The corrected source passes
36 success/allocation/format/lookup/size/init cases with one to four planes.
Two controls compile before exact named failures reject the original errors.
The test checks reference balance and the ordinary handle/destroy path.

`CC`, `CFLAGS` and `TEST_RUNNER` select the host or cross-build execution path.
`GEM_FB_COMPILE_ONLY=1` builds without execution. `GEM_FB_OUTPUT` saves the
positive binary. `GEM_FB_EVIDENCE` must name a new directory for raw inputs and
commands. Use `GEM_FB_SKIP_MUTANTS=1` for a positive ASan/UBSan or supported
Valgrind run. Host instrumentation does not execute the kernel allocator.
This repair is framebuffer memory ownership; DRM/KMS hardware acceptance is
separate.
