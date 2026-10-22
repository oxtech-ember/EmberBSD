# Static Citrus character conversion

Static libc includes a bounded native Citrus module registry for `iconv(3)`.
The registry selects the existing `iconv_std`, `UTF8`, `mapper_646`, and
`mapper_none` implementations. It supports ASCII and UTF8 when the matching
native conversion data is installed. It does not install that data or enable
a multibyte locale. Other modules return the native lookup error.

The static registry uses permanent handles and checks each module/interface
pair. Shared libc keeps its dynamic loader. The additional module objects
enter static, profiling, and debug archives; they do not enter `SOBJS`.

The minimum runtime data is:

- `iconv/iconv.dir`
- `esdb/esdb.dir` and `esdb/esdb.alias`
- `esdb/ISO646/ISO646-US.esdb` and `esdb/UTF/UTF-8.esdb`
- `csmapper/mapper.dir` and `csmapper/ISO646/ISO646-US%UCS.646`

These paths are relative to `/usr/share/i18n`. Generate data with the native
`share/i18n` recipes and host tools. Data presence does not register additional
codecs. A static libc change requires consumers to be relinked from the same
accepted source and library cohort.

## Source contracts

Run `sh ember/tools/citrus-static-contract.sh` with a host C compiler and Ruby.
It compiles complete native translation units, including the public iconv
entry points, registry, dispatchers, codecs, and data parsers. It models only
host ABI details, an ordinary unprivileged process, single-thread locks,
allocation failure, and temporary data paths. It does not model conversion.

The ESDB fixture arrays contain native `mkesdb` output. Their input recipes
and output hashes are recorded in `citrus-static-contract-data.h`. Plain
indexes select the bounded fixture data. The tests cover aliases, identity
and cross-encoding conversion, malformed and incomplete UTF8, output limits,
state/error results, repeated lifetime, missing data, and allocation failures.
Allocation records encode pointer values so the test does not hide leaks by
keeping otherwise unreachable allocations alive.

Set `CITRUS_ANALYZE=1` to run Cppcheck and Clang Static Analyzer on the affected
native modules and the contract. In the dispatcher, CSA enters the generated
`citrus_test_lookup_iconv_entry` wrapper, which calls the actual static parser.
The runner requires syntax/path progress and rejects unexpected diagnostics.
Other selected translation units use full analysis.
Set compiler flags such as
`CFLAGS='-O2 -g -fanalyzer'` or sanitizer flags through the environment.
`CITRUS_TEST_OUTPUT` retains a new absolute output directory with compilation
databases and diagnostics. `CITRUS_COMPILE_ONLY=1` requires that directory.
The runner rejects invalid booleans and test names. `TEST_RUNNER` names one
executable wrapper, such as Valgrind.

Run `sh ember/tools/citrus-static-make-contract.sh` with native make or bmake.
Set `CITRUS_MAKE` to select it. The contract queries the actual native archive
rules for static/shared and profiling/debug configurations. It creates no
target objects. It covers the early `MKLINKLIB` default before `bsd.lib.mk`
defines `MAKELINKLIB`, and explicit enabled/disabled archive overrides.

[The owning CI workflow](../../../../.github/workflows/citrus-static.yml) runs GCC16
path analysis, Cppcheck, CSA, ASan/UBSan, and Linux Memcheck. Native mapper-area
and identity-mapper caches remain reachable for process lifetime. Memcheck
reports these blocks and fails for definite, indirect, and possible leaks,
as well as invalid accesses and uninitialized data.

Host source contracts do not establish a complete target libc build, image
data closure, installed USB descriptor output, or physical device acceptance.
