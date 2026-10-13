# CTF external type units

The OS `ctfconvert` consumes type information from ordinary debug sections,
split objects, supplementary objects and indexed packages. It preserves
separate offset namespaces while resolving references between these inputs.
The implementation is in `external/cddl/osnet/dist/tools/ctf/cvt`; the section
reader belongs to `external/bsd/elftoolchain/dist/libdwarf`.

## Accepted formats and boundaries

The 2026-10-08 acceptance used GCC 16.2.0 and Apple Clang 21 on macOS,
then executed cross-built tools in NetBSD 11.0/AArch64. The 46 format, nine
earlier external, six linked-CU and indexed-operand contracts also passed
with installed `/usr/bin/ctfconvert` and libdwarf 2.2, without a library override. The DWP producer was
[Ports LLVM 23.1.2nb1](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/common-build-tools/cross/llvm-dwp-tests.md).
These are software and VM checks; no physical board or exhaustive DWARF
conformance claim follows from them.

| Input | Checked behavior |
| --- | --- |
| Ordinary DWARF4/5, 32/64-bit offsets | GCC and Clang COMDAT type units, `DW_FORM_ref_sig8`, separate `.debug_types` and DWARF5 `.debug_info` type units |
| Split DWARF4/5, 32/64-bit offsets | GCC and Clang plain/type-unit DWO, GNU DWARF4 indexed strings and DWO identity |
| DWP index version 2/5 | Contribution-relative CU/TU namespaces, shared strings, selected CU and reachable type units |
| Combined split and supplement | DWARF32/64 with supplementary strings, `ref_sup4`/`ref_sup8`, imports and inherited types |
| Linked multiple CUs | Ordinary, split and packaged DWARF4/5; same type/static names retain distinct layouts under distinct source basenames |
| Member layout expressions | A single `DW_OP_plus_uconst` in `DW_FORM_exprloc`, in addition to existing constant member offsets |

Forty compiler format results, four combined-reference results, one member
expression and one mixed ordinary/split abbreviation result match complete
CTF type, object and function output on host and target. Six linked executable
conversions preserve both local layouts and execute successfully. Independent
live GDB checks return the expected values for all six executables.
The earlier external contract also passes nine positive and five malformed
cases. The ordinary reader and link/merge contract still passes all six groups.

DWP indices are validated before their rows are used: table extents, row
coverage, hash placement, duplicate signatures, column identities and selected
contribution bounds. Missing TU indices, truncated tables/contributions and
unsupported trailing member-expression operations fail without rewriting ELF.
Twelve malformed GNU/DWARF5 indexed operands fail at their own CU boundary;
four valid full-width unsigned operands remain readable. The cross-CU read
regression fails with the earlier native library and passes with 2.2.

The converter has specific remaining limits:

- Two distinct CUs with the same source basename and static symbol name are
  rejected. ELF `STT_FILE` supplies only that basename to the existing CTF
  symbol matcher. Address-based symbol association is not implemented.
- Repeated auxiliary section names are rejected as ambiguous. Separate private
  abbreviation tables in ELF groups are not automatically associated with CUs.
- Dynamic member locations, runtime variable locations and arbitrary expression
  evaluation are outside this type consumer. Unsupported member expressions
  fail explicitly. CTF also cannot represent every C++ language construct.
- This work bounds indexed ULEB operands and block payloads. It does not claim
  exhaustive malformed-input hardening of every legacy libdwarf parser path.

## Build and reproduce

First build the host tools using [dtrace-dwarf.md](dtrace-dwarf.md).
The DWP argument below must be executable on the build host. An AArch64
`llvm-dwp` binary cannot run directly on macOS. When the canonical package is
installed on the target, use the supplied SSH adapter:

```sh
src=/absolute/source
out=/absolute/build-output
cc=/absolute/cross/bin/aarch64--netbsd-gcc
export CTF_DWP_SSH_TARGET=your-test-vm
# Optional dedicated host-key file; otherwise normal SSH configuration applies.
export CTF_DWP_KNOWN_HOSTS=/absolute/test-known-hosts
dwp="$src/ember/tests/ctf-dwp-ssh.sh"
sh "$src/ember/tests/ctf-type-units.sh" "$cc" /absolute/clang "$dwp" \
    "$out/tools" /absolute/new-type-fixtures
sh "$src/ember/tests/ctf-multi-cu.sh" "$cc" "$dwp" \
    "$out/tools" /absolute/new-multi-fixtures
```

The adapter copies only the provided DWO files into a private temporary target
directory, runs `/usr/pkg/bin/llvm-dwp`, returns the package and removes that
directory. SSH authentication and known host keys must already be configured.
A native host LLVM installation can be passed directly instead of the adapter.
Every fixture work directory must be new. The type test uses real compiler
output from `type-units.cc`; its C++ wrapper ensures both compilers emit type
units. Controlled assembly fixtures cover combined supplementary references
and malformed operands that ordinary compilers do not emit.

Build the two API contracts against the just-built host libraries:

```sh
for test in section-api index-bounds; do
    cc -O2 -DHAVE_NBTOOL_CONFIG_H=1 \
        -I"$src/tools/compat" -I"$out/tools/include/compat" \
        -I"$out/tools/include" \
        -I"$src/external/bsd/elftoolchain/dist/libdwarf" \
        -I"$src/external/bsd/elftoolchain/dist/libelf" \
        "$src/ember/tests/ctf-dwarf/$test.c" \
        -L"$out/obj/tools/elftoolchain/libdwarf" -ldwarf \
        -L"$out/obj/tools/elftoolchain/libelf" -lelf \
        -L"$out/tools/lib" -lnbcompat -lz -o "/absolute/$test"
done
/absolute/section-api /absolute/new-type-fixtures/gcc-v5-w32-types/original.o
/absolute/section-api /absolute/new-type-fixtures/clang-v5-w32-split-types/mixed.dwo
sh "$src/ember/tests/ctf-index-bounds.sh" "$cc" /absolute/index-bounds \
    /absolute/new-index-fixtures
```

The section API contract checks invalid/non-debug selection, legacy API parity,
ordinary/split auxiliary isolation and DIE traversal after CU iteration ends.

## Native replay

Build `libdwarf` and `ctfconvert` as described in
[dtrace-dwarf.md](dtrace-dwarf.md#native-ctf-execution).
Cross-build the API contracts against the target library rather than the host
archives. For example, after building the target libdwarf directory:

```sh
for test in section-api index-bounds; do
    "$cc" --sysroot=/absolute/complete-sysroot -O2 \
        -I"$src/external/bsd/elftoolchain/dist/libdwarf" \
        "$src/ember/tests/ctf-dwarf/$test.c" \
        -L"$out/obj/external/bsd/elftoolchain/lib/libdwarf" -ldwarf -lelf \
        -o "/absolute/target-$test"
done
```

Copy the generated fixture directories, target contracts and following scripts
to the target. Use matching installed `ctfconvert`/libdwarf 2.2, or a private
converter with `LD_LIBRARY_PATH` pointing to its private library directory.
The dump tool and other library ABIs are unchanged.

```sh
sh ctf-type-units-target.sh /usr/bin/ctfconvert /usr/bin/ctfdump \
    /absolute/type-fixtures /absolute/new-native-types
sh ctf-multi-cu-target.sh /usr/bin/ctfconvert /usr/bin/ctfdump \
    /absolute/multi-fixtures /absolute/new-native-multi
sh ctf-index-bounds.sh replay /absolute/target-index-bounds /absolute/index-fixtures
/absolute/target-section-api /absolute/type-fixtures/gcc-v5-w32-types/original.o
/absolute/target-section-api /absolute/type-fixtures/clang-v5-w32-split-types/mixed.dwo
```

Also replay `ctf-external-target.sh` against its host fixtures. The target
runners compare complete output and check that failures leave inputs unchanged.
