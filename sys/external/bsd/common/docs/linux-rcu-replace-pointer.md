# Linux rcu_replace_pointer

The Linux compatibility header
`sys/external/bsd/common/include/linux/rcupdate.h` defines
`rcu_replace_pointer(P, NEWPTR, C)`. As in Linux, `P` is the RCU-protected
pointer lvalue. The macro stores `NEWPTR` in `P` and returns the old value.

The previous macro copied `P` into a local variable. It stored `NEWPTR` in
that copy and returned the old value. The real pointer did not change.

The only consumer is i915 `set_engines()`, for `I915_CONTEXT_PARAM_ENGINES`.
Both a new engine map and a reset to the default map use it.
It passes the returned pointer to `call_rcu()` as the old engine set.
With the previous macro, these errors occurred:

- The context kept the old engine set, but the RCU callback freed it.
- Context clone and context free used the freed set.
  `i915_gem_context_free()` freed it a second time.
- The callback released the old `intel_context` objects and destroyed their
  mutexes.
- The new engine set leaked.
- The context still reported user engines, so `execbuffer` used the user
  ring index on the freed set.

Mesa 26 Iris sets an engine map when it creates a context. Its first
`execbuffer` then pinned a destroyed context. On a Jasper Lake laptop the
kernel panicked:

```
trap type 6 cr2 0xfffffffffffffff0
mutex_vector_enter()
intel_context_alloc_state()
__intel_context_do_pin()
i915_gem_do_execbuffer()
```

`mutex_destroy()` stores `MUTEX_THREAD` (`-16`) in the owner word.
`mutex_vector_enter()` treats it as an owner LWP, and `mutex_oncpu()`
reads `l_cpu` at offset 0 of that address.

## Regression

```sh
sh ember/tools/linux-rcu-replace-contract.sh
```

The contract compiles the actual header with host models of its kernel
headers. It replaces a context engine pointer twice. It checks the stored and
returned values, the `C` condition and one evaluation of `NEWPTR`.
It also requires the `set_engines()` consumer form.
The previous macro fails the first stored-pointer check.
