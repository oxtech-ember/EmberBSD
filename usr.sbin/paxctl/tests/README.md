# paxctl native write regression

Run on a little-endian host with the selected native SDK tools:

```
CC=/path/to/sdk/bin/clang /path/to/sdk/bin/python3 tests/test-pwrite.py
```

The test compiles the actual `paxctl.c` with a small host compatibility shim
and an injected `pwrite`; no installed executable or target file is modified.
It checks an EIO failure, a real partial write, and a normal successful update
and readback on disposable ELF32 and ELF64 PaX fixtures in both byte orders. `PAXCTL_SOURCE` may select
another OS source tree for a baseline comparison. A partial write can modify
part of the file; the contract is that the command reports failure.
