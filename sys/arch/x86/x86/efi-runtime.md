# EFI runtime memory and KASAN

EFI runtime mappings can use physical addresses as virtual addresses.
They can also use virtual addresses selected by the boot loader.
These addresses belong to `efi_runtime_pmap` and have no kernel KASAN shadow.

The runtime-table copy therefore calls `(memcpy)` while that pmap is active.
This call uses the raw copy function for this foreign-memory boundary.
Ordinary kernel copies keep the instrumentation in `libkern.h`.
KASAN configuration and address checks remain unchanged.

This fix comes from NetBSD commit
[`c8b00f7c39ae397b5be1bbe67e2500d042d3bdda`](https://github.com/NetBSD/src/commit/c8b00f7c39ae397b5be1bbe67e2500d042d3bdda)
by Taylor R. Campbell, for
[PR kern/60391](https://mail-index.netbsd.org/netbsd-bugs/2026/07/01/msg093543.html).
The imported code keeps its NetBSD license and revision identifiers.

Run `ember/tests/efi-runtime-copy-contract.py` with explicit `HOST_CC` set
to a host GCC 16 executable. It extracts the production copy block and
the production wrapper declarations. It checks copy contents, pmap ordering,
ordinary and KASAN dispatch, and two controls that must exit 1.
One control restores the old call. The other removes ordinary instrumentation.
The test runs no firmware and does not emulate KASAN shadow memory.
Full kernel builds and EFI boot acceptance remain separate checks.
