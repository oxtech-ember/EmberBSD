# Native flex filter argument contract

The imported flex passes an external filter argument list to `filter_create_ext`.
That variadic function reads each argument as `const char *`, including its null
sentinel. An integer `0` does not have that type in a variadic argument list.
The call now passes an explicit null `const char *`.

On an ARM64 macOS host with GCC 16.2, the old call stores only four zero bytes in
the variadic argument slot. The callee reads eight bytes. Remaining stack bytes
can become an invalid argument pointer. The real native `nblex` then fails to
execute `nbm4`, even when `nbm4` works independently. The observed failure exits
with SIGPIPE and leaves an empty generated source file. A later incremental
build can compile that empty file and report missing lexer symbols at link time.

## Regression

```sh
CC=/absolute/native/gcc-16 \
CPPFLAGS='-isysroot /absolute/macos-sdk' \
LDFLAGS='-isysroot /absolute/macos-sdk' \
ruby ember/tests/flex-filter-sentinel.rb \
    /absolute/source-tree /absolute/new-results
```

The test extracts the actual constructor, record and caller from the source
tree. It adds the documented sentinel annotation to the test declaration so
the old integer sentinel fails compilation with warnings treated as errors.
It then checks the exact two arguments and null pointer across 1000 allocations.
The test frees every allocation. It does not substitute a filter constructor.

For a supported native Clang, run the same command with:

```sh
CC=/absolute/native/clang \
CFLAGS='-O1 -g -std=gnu17 -fsanitize=address,undefined -fno-omit-frame-pointer' \
ruby ember/tests/flex-filter-sentinel.rb \
    /absolute/source-tree /absolute/new-sanitizer-results
```

The native GCC16 boundary test and Clang ASan/UBSan test pass on ARM64 macOS.
A private native flex relink with the corrected `main.c` also generates a
complete racoon `cftoken.c`. Compiling that generated file with the real amd64
racoon flags produces ELF definitions for `yylex`, `yywarn`, `yyerrorcount`,
`yycf_switch_buffer`, `yycf_init_buffer` and `yycf_clean_buffer`.

The private relink reuses unchanged native flex and compatibility objects.
It is a focused host-tool result. Rebuild the normal native tool before
regenerating failed outputs in the main object tree. An existing empty
`cftoken.c` must not be accepted as a successful regeneration. This check does
not establish a complete racoon link, running daemon or OS distribution.

## Analysis limits

GCC16 `-fanalyzer` passes the exercised constructor boundary. Full native
`main.c` analysis retains the same pre-existing floating-point allocation-size
warning at `check_options` before and after the fix. It is outside this sentinel
change. Cppcheck 2.22 runs on both boundary variants with all categories enabled;
it reports standard-header lookup information and no code findings there.
Full native preprocessing retains the actual compiler flags, headers and macros,
but Cppcheck cannot parse the Darwin SDK `errno_t` declaration. No clean full-TU
Cppcheck result is claimed. ASan/UBSan is host execution evidence, not Valgrind
or target execution evidence.
