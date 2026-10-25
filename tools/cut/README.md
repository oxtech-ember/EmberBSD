# Native bootstrap cut

[bootstrap.json](bootstrap.json) selects the existing [cut source](../../usr.bin/cut/cut.c)
and its included [byte/character template](../../usr.bin/cut/x_cut.c).
The C implementation, options, BSD licenses and original notices stay unchanged.
The [host recipe](Makefile) uses the normal OS compatibility library.
The target recipe keeps `-lutil`; the host recipe uses the selected compatibility library.

The finite SDK route uses the [common efun subset](../compat/README.bootstrap.md).
It compiles cut and four complete compatibility C files after actual capability probes.
It creates no private allocator implementation and does not discard unused efun functions.
The compiler tuple, native ABI, source proofs and observation plan remain SDK inputs.
No target headers, historical compatibility archive or system build tool is selected.

The 22 C-locale cases cover stdin, files, byte and character ranges, fields,
delimiters, suppression, allocation growth and invalid input.
They retain exact streams and exits from this owner contract.
The `-n` cases use ASCII; they do not establish multibyte locale coverage.
The first case reads `input.txt` for the final direct runtime observation.
Source checks alone do not establish native compilation, C analysis or memory acceptance.
