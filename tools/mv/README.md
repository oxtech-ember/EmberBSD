# Native host mv

This entry builds the complete canonical `bin/mv` through `Makefile.host`.
It uses the normal `tools/compat` library and generated configuration.
It does not select a reduced CLI or a private copy implementation.

Set `HOST_MV_CP` and `HOST_MV_RM` to absolute executable paths from the
selected native SDK. The build compiles these paths into `mv`.
The selectors accept ASCII letters, digits, slash, dot, underscore, plus,
and hyphen. They do not accept shell expressions or whitespace.
Verify the selected programs and their runtime dependencies before building.
The host program never substitutes `/bin/cp`, `/bin/rm`, or a PATH lookup.
Target and rescue builds retain their existing paths.

Same-filesystem moves use `rename`. Actual `EXDEV` selects the complete
regular-file copy or the selected recursive `cp` and `rm` children.
The source remains after data, close, or child-copy failure.
Characteristics failures retain the canonical diagnostic and exit policy.
Failed ownership changes clear set-user-ID and set-group-ID mode bits.
The host path applies ownership and mode before the common ACL/xattr copy.
It keeps both descriptors open for that copy and applies file flags last.

Host times use the shared full-timespec stat accessors and native `futimens`.
The host must provide that FD interface. The build must not silently reduce
nanosecond precision to `futimes`. Flags use the configured `st_flags` member.
Hosts without that member have no BSD file flags to copy.
`SIGINFO` progress remains available on hosts which define that signal.

`bin/mv/test-source.py` and `test-host.py` check the source contracts.
`test-selectors.py` checks the actual recipe fragment with an explicit SDK
bmake, makefile directory, shell, and cp/rm paths. It does not build mv.
`test-close.py` and `test-child.py` emit focused diagnostics from canonical
code. Their controlled failures do not prove real cross-filesystem moves.
Native checks must use two owned roots on different devices and verify
real `EXDEV`, data and metadata preservation, child errors, and source states.
`test-native.py --program /absolute/SDK/mv --work /fresh/owned/root`
checks same-filesystem operations. Add `--other-work /fresh/other-device/root`
for real EXDEV checks. Darwin storage must include a `.noindex` path component;
this reduces indexing interference, but does not prevent arbitrary readers.
Both roots retain their fixtures and result files for inspection.
These cases do not inspect nonempty ACL/xattr values or nested loader routes.
Cppcheck, path analysis, memory checks, other hosts, and full native operation
remain separate from source checks.
