# FDT register range conversion

`fdtbus_get_reg` converts the uint64 decoder result to the native bus address
and size types. It checks addition overflow and exact narrowing before it
writes either output. A 32-bit range may end at 4 GiB. The address and size
must each fit their native type. An empty range preserves the existing API
behavior when its address fits.

The [source check](../../../../ember/tools/fdt-reg-contract.rb) compiles the complete actual
function with controlled uint64 decoder results. It models 32-bit and 64-bit
bus types. The cases cover wrapped addition, narrowing, exact end boundaries,
NULL outputs, decoder errors and unchanged output storage on rejection.
It does not test the full FDT parser, range translation, device access or
framebuffer ownership. A caller must still validate its cells, descriptor,
full mapping span and page rounding before mapping a framebuffer.

```sh
ruby ember/tools/fdt-reg-contract.rb /absolute/os/source
```

`CC` selects one compiler executable. `CFLAGS` supplies its argument list.
`TEST_RUNNER` supplies an optional executable wrapper and arguments.
`FDT_REG_COMPILE_ONLY=1` supports cross compilation without execution.
`FDT_REG_EVIDENCE` saves the actual function and command receipt.
`FDT_REG_OUTPUT` saves the executables with `32` and `64` suffixes; its parent
must exist. `FDT_REG_INPUT` selects a saved original source for a causal check.
A failure in the original function is separate from a compiler failure.
