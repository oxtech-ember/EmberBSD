# Initial date parser compatibility component

[bootstrap-parsedate.json](bootstrap-parsedate.json) selects the complete
[canonical grammar](../../lib/libutil/parsedate.y).
The grammar, lookup tables, relative operations and error paths remain intact.
The original authors and public-domain notice remain in that source.
The native libutil target keeps its original `mktime_z` call.

The host build prefers a declared and linked `mktime_z(NULL, ...)`.
Otherwise it uses a declared and linked `timegm`.
The same [probe sources](probes) serve normal configure and the finite component.
The [feature header](compat_parsedate_features.h) requests native extension declarations.
It does not assert that a function exists. No host name selects an implementation.
Neither route changes `TZ` inside the parser.
The existing `errno`, normalization checks and timezone arithmetic still apply.

## Normal host integration

[parsedate.mk](parsedate.mk) selects the generated source and the `__pd` prefix.
The normal library uses that fragment and [compat_parsedate.h](compat_parsedate.h).
It generates the complete grammar with an explicit absolute `HOST_YACC`.
The caller must supply the selected SDK Byacc before building `tools/compat`.
The later `tools/yacc` target cannot bootstrap this dependency.
The [native tool contract](../native-tools/contract) binds `yacc` to `HOST_YACC`.
The build wrapper preserves and protects that selection.
An absent or relative selector stops the normal compatibility build.
Tool identity, ABI flags and immutable input lifetime remain SDK responsibilities.

Normal configure checks both UTC declarations and links with the selected compiler.
It stops when neither backend is available.
Its generated script and header template carry the same checks.
Regenerate them with selected SDK Autoconf tools when those tools are available.

## Finite SDK contract

The distinct stage is `compat-parsedate-bootstrap`.
The initial finite route requires the native `timegm` control.
It tests UTC conversion with a fixed non-UTC timezone and a valid `time_t -1`.
Both probes run after that control. Their actual results set
`HAVE_MKTIME_Z` and `HAVE_TIMEGM` in `nbtool_config.h`.
The route requires `HAVE_TIMEGM=1` and records the actual first probe result.
The generated header is a component subset of normal configuration.

The bounded plan has thirteen commands:

1. `parsedate-control-compile`
2. `parsedate-control-link`
3. `parsedate-control-run`, with runtime diagnostics
4. `func-mktime_z`, the declaration and link probe
5. `func-timegm`, the declaration and link probe
6. `generate-parsedate`, selected Byacc with `-p __pd -o parsedate.c`
7. `compile-parsedate`, selected GCC16 with the generated configuration
8. `archive-parsedate`, selected ar with the single `parsedate.o` member
9. `index-parsedate`, selected ranlib
10. `compile-parsedate-check`
11. `link-parsedate-check`, linked with the new `libnbcompat.a`
12. `parsedate-check`
13. `runtime-observation`, a second check run with separate runtime diagnostics

The generator consumes the pinned full grammar. No parser body is copied into SDK.
Its output must be absent before generation and retained after the child drains.
Bind its bytes and mode before each later command and in the final candidate.
Keep bounded regular-file and hash checks for headers, objects, archive and sources.
The separate export admission also requires read-only export modes.
Unexpected work entries and missing or changed outputs must stop the producer.
Keep separate runtime traces for the control and check images.

The five exports are `libnbcompat.a`, `nbtool_config.h`, `compat_parsedate.h`,
`compat_parsedate_features.h` and `bootstrap_cdefs.h`.
The archive has exactly one canonical parser object.
SDK owns the bounded runner, provenance checks and scoped component receipt.
The existing file-time stage and receipts remain separate inputs.

## Checks and limits

The [source checks](test-parsedate.py) use Python's standard library only.
They check complete parser preservation and normal/finite source agreement.
They do not generate a parser or execute C code.

The [C check](probes/parsedate-check.c) uses fixed times and fixed POSIX zones.
It covers UTC, explicit offsets, local time, relative days, month clamping,
valid negative epochs, preserved errno, malformed input, invalid dates and overflow.
It also retains the parser's existing fractional-second truncation.
These cases need execution through the selected SDK producer before acceptance.

This source change does not qualify touch, an ordinary SDK or another host ABI.
Nanosecond parsing, timezone database transitions, other time ranges, C analysis
and memory checks need separate evidence.
The native libutil tests remain the wider parser test suite.

The parser contract follows NetBSD.
[OpenBSD touch](https://github.com/openbsd/src/blob/master/usr.bin/touch/touch.c),
[FreeBSD touch](https://github.com/freebsd/freebsd-src/blob/main/usr.bin/touch/touch.c)
and [DragonFly touch](https://github.com/DragonFlyBSD/DragonFlyBSD/blob/master/usr.bin/touch/touch.c)
also use `timegm` for explicit UTC input.
Their smaller date grammars do not replace the canonical parser.
