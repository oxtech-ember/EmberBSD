# libintl singular and plural validation

EmberBSD's base libintl now preserves ordinary translations in languages
whose plural expression returns a nonzero index for one item. Previously,
Welsh `dgettext("Norway")` returned only `y` instead of `Norwy`. This made
KI18n's country-name search ambiguous even though the installed iso-codes
catalog was correct. Missing plural translations also returned the singular
source string for counts other than one.

The fix stays in [the base library](../gettext.c). Singular
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

## ABI and validation scope

The accepted shared and static consumers each passed 100 checks across both
catalog forms. The unchanged KI18n 6.31 country suite with iso-codes 4.20.1
passed 22 checks. These bounded results establish the translation fix,
not a complete Plasma session or long-duration stability.

The shared library preserves exported symbols and SONAME `libintl.so.1`.
Its only NEEDED library is the fork's `libc.so.12`. Profiling archives were
not rebuilt by this focused check; a complete base release must use the normal OS build.
Cppcheck covered the implementation and regression consumer. GCC `-fanalyzer`
covered the changed implementation.
This result includes no accepted Valgrind run on EmberBSD/AArch64.

The [pinned original acceptance record](https://github.com/oxtech-ember/EmberBSD/blob/4a4c98bb99999836103d3fe63fd6f4c98013f0c5/ember/boot/libintl-validation.md)
retains exact binary hashes, source context and analyzer coverage.
