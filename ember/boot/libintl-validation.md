# libintl singular and plural validation

EmberBSD's base libintl now preserves ordinary translations in languages
whose plural expression returns a nonzero index for one item. Previously,
Welsh `dgettext("Norway")` returned only `y` instead of `Norwy`. This made
KI18n's country-name search ambiguous even though the installed iso-codes
catalog was correct. Missing plural translations also returned the singular
source string for counts other than one.

The fix stays in [the base library](../../lib/libintl/gettext.c). Singular
gettext/context calls bypass plural-form selection. A missing translation
uses the original singular/plural argument according to the count.
Citrus authorship and the existing ABI are retained.

## Reproduce

Use the current EmberBSD source checkout, its verified sysroot and GCC16
cross tools. GNU msgfmt runs on the build host. A complete checkout must
include `lib/libintl` and the four Citrus headers used by its plural parser.

```sh
CROSS_CC=/absolute/cross/bin/aarch64--netbsd-gcc \
CROSS_AR=/absolute/cross/bin/aarch64--netbsd-ar \
SYSROOT=/absolute/ember-sysroot MSGFMT=/absolute/native/msgfmt \
sh ember/tests/libintl-singular.sh /absolute/new-work
```

The builder compiles the actual seven library sources, a shared library,
a static archive and two consumers. Its receipt records source hashes,
the source revision/diff, tools and relevant sysroot inputs. It does not
install anything. Copy the generated directory to EmberBSD, without host
extended attributes, and run with a timeout:

```sh
timeout -k 3 20 sh run.sh fixed
```

Each consumer performs 50 assertions against both hashed and binary-search
catalogs: six singular/context entry points, six plural/context entry points,
all six Welsh plural forms and untranslated fallbacks. The static consumer
has no libintl runtime dependency. `sh run.sh system` uses the installed
shared library; on the old library the first catalog exposes 11 failures.

## CM5 acceptance, 2026-10-12

The physical board was CM5 Rev 1.0, BCM2712 D0, 4 GiB on Waveshare CM5-NANO-B.
Kernel `EMBERV3DRENDER #1`, SHA256
`6c5aa74d3c33fb6e84ea5c04d6f3df48928deb021cdc908ee972a42df03c7bf5`,
remained unchanged during the checks. This is a short functional run,
not a long-duration stability result.

| Check | Result |
| --- | --- |
| Old shared library, first fixture catalog | 50 checks, 11 failures |
| Fixed shared library, both catalog forms | 100 checks, zero failures |
| Fixed static archive, both catalog forms | 100 checks, zero failures |
| Original KI18n 6.31 country suite, iso-codes 4.20.1 | 22 passed, zero failures |
| Cppcheck 2.22, library implementation and regression consumer | No findings |
| GCC16 `-fanalyzer`, changed implementation | No findings |

The fixed shared library retains all previously exported symbols and SONAME
`libintl.so.1`; its only NEEDED library is the fork's `libc.so.12`.
Shared/static outputs were installed together on CM5 and in the build sysroot,
with verified backups. Profiling archives were not rebuilt by this focused
check; a complete base release must use the normal OS build.

| Output | SHA256 |
| --- | --- |
| Previously installed shared library | `271297e655ee4745e13bdd087aecaaf0706c477eb5a24c9d69ce667bae2a5292` |
| Fixed shared library | `90f5dcadb52fcdaa919ddea1dcac2ba542455563933f0bdc09e1a4f09a0c3acb` |
| Fixed static archive | `e4122e27b773a795e65b6c4f40931061c96cb696015f775d08887e0ca67034a4` |

This fixes a localization dependency of Plasma. It does not establish a
working Plasma session. Valgrind has no accepted EmberBSD/AArch64 runner here.
