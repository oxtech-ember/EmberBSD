# Native bootstrap sort

[bootstrap.json](bootstrap.json) selects the nine complete C files in the
[canonical recipe](../../usr.bin/sort/Makefile) and its three local headers.
The C implementation, options and original BSD notices remain unchanged.
The [host recipe](Makefile) uses the normal OS compatibility library.
The target recipe retains `-lutil`.

The initial SDK route reuses the [common efun subset](../compat/README.bootstrap.md).
It compiles all four canonical compatibility sources after the same five capability probes.
The generated configuration contains actual compiler observations, not assumed host features.
No private allocator, compatibility archive or probe implementation is introduced.
All 35 source members belong to the selected OS revision.

The 36 cases follow the existing [sort tests](../../tests/usr.bin/sort/t_sort.sh).
They cover ordinary input, NUL bytes, partial final lines, numeric and field keys,
stability, character filters, merging, uniqueness, order checks and error exits.
The locale is `C`. The native producer selects the compiler, ABI, shell and temporary directory.
The first file case also supplies the final direct runtime observation.

The 131073-record fixture exceeds the implementation's 131072-record buffer limit.
Its ordinary and unique cases exercise temporary-file sorting and merge output.
The temporary directory is the owned work directory. Debug shortcuts are not used.
The long-line fixture exceeds 64 KiB and exercises keyed line-buffer growth.
Both fixtures use the shared bounded data decoder and exact expected bytes.

This initial profile does not exercise `-o`, file replacement permissions, non-C locales,
or long-line order checking with `-c`. These capabilities remain in the implementation.
Source checks do not establish native execution, C analysis or memory acceptance.
