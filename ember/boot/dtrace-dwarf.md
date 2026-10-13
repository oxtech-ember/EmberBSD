# Live DTrace and external DWARF

EmberBSD keeps GCC's DWARF5 default. Its CTF tools read ordinary C debug
units, split and supplementary objects, indexed packages and type units. The
FBT provider reads both CTF2 and CTF3, including the running GCC16 kernel's
argument types. This allows a D script to inspect kernel structures rather
than receive untyped probe arguments.

These adaptations belong to the OS repository. They preserve the original
NetBSD, elftoolchain and CDDL authorship. The new adaptations and contracts
are AI-assisted EmberBSD work; they have not been accepted upstream.

## Accepted scope

On 2026-10-08, Apple Silicon macOS cross-built the tools and modules with
GCC 16.2.0. Native execution used NetBSD 11.0/AArch64 in UTM, four CPUs and
4 GiB RAM. The VM runs the `EMBER64` kernel built from
`21cd2464c720159bec0a3ba352e4dee940a58b02`, SHA256
`d79c53a823855211c613245808c6f588d418c9e5bd83990ff3dc60cee643b54c`.
The modules use that source export and compiler with this change applied.
The FBT module includes the repaired `kern_ctf.c` reader.

| Consumer | Executed check |
| --- | --- |
| Host CTF tools | Six ordinary DWARF groups, including GCC/Clang DWARF4/5, DWARF32/64, string bounds and merging |
| Host external-DWARF reader | GCC/Clang DWARF4/5 DWO/DWP32/64, signature type units, combined supplements and multiple primary CUs |
| Native CTF tools | 46 format results, nine earlier external cases and six linked multi-CU results match host output; bounds/API and atomic rejection checks pass |
| FBT decoder | ASan/UBSan, CTF2/3 widths, large type IDs, truncated records and all 20,771 types in the actual kernel CTF |
| Live DTrace | Syscall and FBT entry/return probes, profile ticks and `args[0]->l_proc->p_pid` dereferences |

The final live run reported `calls=101 returns=101 fbt=101 fbt_returns=101
typed=101 ticks=21` and exited successfully. This is a bounded VM run, not
physical-board acceptance, a soak test or coverage of every DTrace provider.
Loading a module or listing probes alone is insufficient evidence.

CTF conversion accepts multiple primary CUs and skeletons, indexed DWP packages,
signature-referenced type units and combined split/supplementary inputs.
Static symbols with ambiguous source basenames and duplicate private auxiliary
sections are rejected explicitly. Arbitrary runtime expressions remain outside
this type consumer. See [CTF external type units](ctf-external-types.md) for the
exact matrix, remaining limits and reproducible host/native checks.
CTF acceptance does not establish every debugger's DWARF support.

Separately, [Ports GDB 18.1nb1](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/gdb)
is installed as a pkgsrc package and selected by `/usr/bin/gdb`. It passes
32 format, 24 expression, 14 agent-expression and 72 entry-state checks,
plus the external-object and live FP-register/signal-unwinding suite.
General entry operands now evaluate constants, nested procedures and
reconstructible register arithmetic, including float/SIMD values. Missing
historical state remains unavailable rather than being replaced by current state.

The common [LLVM 23.1.2nb1 package](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/llvm-dwp-tests.md)
passes 104 DWP creation/repackaging checks, including mixed offset widths,
shared indexed CU/TU tables and string-offset promotion. Both packages retain
normal pkgsrc integrity checks. These executed profiles do not establish
universal DWARF conformance across every language and producer extension.

The [development image](../image/README.md) owns offline package installation
and reboot acceptance. The base source import remains GDB 15.1; image assembly
removes its executable entry points and selects the current Ports package.

## Build and check the host readers

Use the complete source export and external toolchain described in
[cross-build.md](cross-build.md). Preserve its source revision, compiler,
kernel configuration and output hashes. With the generated make wrapper:

```sh
mk=/absolute/output/tools/bin/nbmake-evbarm
src=/absolute/source
"$mk" -C "$src/tools/elftoolchain/libdwarf" dependall install
"$mk" -C "$src/tools/ctfconvert" dependall install
"$mk" -C "$src/tools/ctfdump" dependall install
"$mk" -C "$src/tools/ctfmerge" dependall install
sh "$src/ember/tests/ctf-dwarf.sh" /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/output/tools /absolute/new-ordinary-tests
sh "$src/ember/tests/ctf-external.sh" /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/output/tools /absolute/new-external-tests
```

Each work directory must be new. The external test uses real compiler DWO
output and the assembly fixture in `ember/tests/ctf-dwarf/supplementary.S`.
It verifies DWO identities and matching supplementary checksums. Negative
cases cover missing files, wrong identities, truncated headers/references,
cross-object offset collisions and invalid CU-relative inherited references.
Failed conversion must leave the input ELF byte-for-byte unchanged.

The [Ports cross compiler](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/cross)
installs binutils in GCC's target-tool search directory. Its split-DWARF
contract runs with a clean PATH and no `-B` workaround for `objcopy` lookup.

The FBT decoder contract is independently runnable on the host:

```sh
/absolute/cross/bin/aarch64--netbsd-objcopy \
    --dump-section .SUNW_ctf=/absolute/kernel.ctf /absolute/matching/netbsd
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$src/ember/tests/fbt-ctf.c" -lz -o /absolute/fbt-ctf
/absolute/fbt-ctf /absolute/kernel.ctf
```

## Native CTF execution

Cross-build the target libraries `external/bsd/elftoolchain/lib/libelf`,
`external/bsd/elftoolchain/lib/libdwarf`, `lib/libz` and
`external/cddl/osnet/lib/libctf`, then
`external/cddl/osnet/usr.bin/ctfconvert`, with the same make wrapper.
Run `obj` before `dependall` for a newly selected directory.
Use a complete target `DESTDIR`, including base headers and startup objects.
A partial tool-only destination is insufficient.

For an isolated build against an older installed header, pass
`CPPFLAGS.dwarf.c=-I/absolute/source/external/bsd/elftoolchain/dist/libdwarf`
to the converter's make invocation. A full build installs the new header
normally. Do not replace the complete CPPFLAGS include configuration.

The exported `dwarf_elf_init_section` API advances libdwarf from 2.1 to 2.2;
the ABI major remains 2. It selects an isolated ordinary or split section,
retaining the original `dwarf_elf_init` behavior when the index is zero.
Install the matching library and converter together.
Install both public headers, `libdwarf.h` and `dwarf.h`, with the library.
Copy the host external-test directory and run on the target:

```sh
sh ember/tests/ctf-external-target.sh /usr/bin/ctfconvert /usr/bin/ctfdump \
    /absolute/host-external-tests /absolute/new-native-tests
```

The `dwarf_set_tied_dbg` API borrows a caller-owned supplementary context. The caller validates
file identity, keeps it alive and resolves supplementary reference offsets
in that object's namespace. See `dwarf_set_tied_dbg(3)` for its lifetime and
error contract. It does not search arbitrary files on behalf of callers.

## Live tracing in the matching VM

Build `sys/modules/solaris`, `sys/modules/cyclic` and `sys/modules/dtrace`
with `obj` followed by `dependall` through the same make wrapper. The last
directory recursively builds its providers. Keep these modules with their
matching kernel; do not force-load modules from a different kernel build.
The checked set is `solaris`, `cyclic`, `dtrace`, `dtrace_sdt`, `dtrace_fbt`,
`dtrace_lockstat`, `dtrace_profile` and `dtrace_syscall`.

Copy the eight `.kmod` files into a private test directory on the VM.
Use root in a test VM permitting module loading and DTrace. Check
`uname -a`, the kernel hash and `modstat` before changing loaded modules.
Load missing modules in the order above with `modload /absolute/name.kmod`.
Do not unload modules in use by another tracing session.

Cross-compile and copy the bounded workload to that VM:

```sh
/absolute/cross/bin/aarch64--netbsd-gcc -O2 -Wall -Wextra -Werror \
    ember/tests/dtrace-live.c -o /absolute/dtrace-live
```

Run the acceptance script using the target DTrace executable:

```sh
/usr/sbin/dtrace -q -s ember/tests/dtrace-live.d -c /absolute/dtrace-live
```

The D script checks entry/return counts and typed reads, handles DTrace
errors, and has a ten-second watchdog. The workload validates its own
syscall results. The decoded CTF extent is bounded before FBT walks its
tables; CTF3 uses 32-bit type IDs and different function-record alignment
from CTF2. Both formats remain supported.
