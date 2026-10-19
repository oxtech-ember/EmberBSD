# Host Binutils parser sources

Normal host-tools builds use the parser C files shipped in the source tree.
The GNU make path restores the standard Automake non-maintainer guards for lex
and yacc. It does not rewrite an existing parser during build or installation.
Missing outputs still run the generator and report its errors. This matches the
source-preservation policy of the BSD make wrapper in `tools/Makefile.gnuwrap`.

After changing a grammar, maintainers must regenerate and review its C output.
Use `BINUTILS_REGENERATE_PARSERS=yes` in the host Binutils make invocation, or
pass `-V BINUTILS_REGENERATE_PARSERS=yes` to `build.sh`. This opt-in allows source
writes. Normal mode does not prove that an edited grammar matches shipped C.
Build admission must separately check the selected source revision and changes.

Run the host regression with an existing `build.sh` make wrapper:

```sh
python3 ember/tests/binutils-parser-source.py \
    --source /absolute/EmberBSD \
    --make-wrapper /absolute/output/nbmake-evbarm
```

The test extracts the upstream GNU rules and expands the actual BSD build
command. It executes recursive GNU make with the wrapper's lex and yacc tools.
It checks unchanged bytes and modes of read-only parser sources, generation of
missing outputs, generator failures, and explicit maintainer regeneration.
The maintainer case reproduces the previous source-write behavior. Temporary
fixtures are removed automatically. The test does not execute target binaries.

The guards follow the expressions in imported Automake-generated
`external/gpl3/binutils/dist/ld/Makefile.in`. The build integration and causal
test were prepared with AI assistance; imported source licenses remain intact.
