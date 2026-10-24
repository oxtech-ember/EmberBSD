# Native bootstrap grep

[bootstrap.json](bootstrap.json) describes the existing host grep sources.
The [host recipe](Makefile) remains the canonical owner of the feature flags.
The [program recipe](../../usr.bin/grep/Makefile) owns the ordered five C files.
Their BSD licences and original copyright notices remain unchanged.
The manifest adds no copy of the implementation or build framework.

Use this small source entry when an SDK bootstrap needs grep before package
metadata or configure tools are available. The SDK owns input acceptance,
GCC16 selection, native ABI inputs, process control, resource limits and evidence.
The source entry does not qualify a seed or an ordinary native toolset.

## Source inputs

Pin the OS commit and the exact manifest hash in the SDK producer plan.
Inventory every listed source and header at that commit.
Keep both canonical makefiles as provenance inputs too.
Reject a changed source order, extra source, extra flag or different compiler.
This version requires the exact three `WITHOUT_NLS`, `WITHOUT_BZ2` and
`WITHOUT_GZIP` defines already used by `tools/grep/Makefile`.
They select the established host-tool scope. The target grep recipe is unchanged.

The manifest has no extra C flags, link flags or libraries.
The selected native ABI supplies the platform headers and libraries.
Do not put target OS headers or a target compatibility library in that ABI.
A host must provide the required BSD libc interfaces, including `REG_STARTEND`,
`fts`, `getopt_long`, `getline`, wide-character functions and the source's
`__progname`, `__RCSID` and `__dead` interfaces.
Do not guess compatibility defines or suppress a missing-interface diagnostic.
Use [the tools portability procedure](../README) for a real missing feature.

The normal tools build uses `tools/Makefile.host` and its compatibility layer.
This bounded direct-source entry uses a native ABI that provides these interfaces.
It does not establish that the full tools build or other host platforms pass.
It needs no generated source, configure output or external library in the
listed host profile. Compiler and linker execution still need acceptance.

## SDK compile and link contract

Load one accepted SDK selection before the first child process.
For this bootstrap scope, use the existing immutable `BootstrapSeed` API.
Require its `cc` role and accepted native ABI flags.
Do not infer GCC16 qualification from an executable name or version string.

For each source, construct one C compile command in manifest order:

```text
selection.command('cc')
+ selection.abi_flags['cppflags']
+ selection.abi_flags['cflags']
+ manifest.cppflags
+ ['-I', source_root / 'usr.bin/grep']
+ ['-c', source_root / source, '-o', owned_object]
```

Link the five owned objects in the same order:

```text
selection.command('cc')
+ selection.abi_flags['cflags']
+ owned_objects
+ selection.abi_flags['ldflags']
+ ['-o', owned_output / 'grep']
```

Each list item is an argv item. Do not parse a shell command from the manifest.
Keep the selected driver's fixed binding once in `selection.command('cc')`.
Keep source files, objects and outputs inside the producer's pinned roots.
The SDK's shared runner owns child records and resource leases.
This entry does not provide another compiler launcher or executor.

## Finite runtime contract

`runtime_cases` contains fixed stdin, file, argv and output data for this program.
Create `files` only in the new owned runtime directory.
Keep `missing.txt` absent. Run with that directory as cwd and `LC_ALL=C`.
Do not turn these case data into a general shell-command interface.

`grep` is the linked output. Create owned relative `egrep` and `fgrep` symlinks
to it for the two invocation-mode cases. These are this program's documented
invocations. They do not rename a compiler or relabel a provider input.
Use the SDK stdin facility or the selected shell's bounded builtin input path.
Do not discover `cat`, `echo` or another utility from the host PATH.

Successful cases require exact stdout and empty stderr.
No-match cases require exit 1 and empty output.
Error cases require exit 2, empty stdout and nonempty stderr.
The error text is not fixed because native libc diagnostics differ.
If the runner combines streams, retain that limit and check the combined bytes
only where it can establish the expected result. Do not claim separate stream
qualification from a combined log.

Record actual compile/link children, native Mach-O load paths and runtime results.
Check that no child or library resolves through an unselected prefix.
The fixed cases cover stdin and file inputs, BRE/ERE/fixed matching, quiet mode,
line numbers, multiple patterns, invocation modes and the 0/1/2 status contract.
They do not replace the full grep tests, Unicode/large-input checks or memory analysis.
Source validation alone does not establish a runnable native grep.
