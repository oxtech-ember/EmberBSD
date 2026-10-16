# Optional certificate metadata in syslogd TLS status

`usr.sbin/syslogd/tls.c` can initialize TLS without a common name or a fingerprint
for its status message. `get_commonname` returns false and NULL for a certificate
without a CN. `get_fingerprint` also has failure paths that return NULL.
A later status-only certificate read can fail after the TLS context loads.
The status formatter must not pass these optional pointers to `%s`.

The formatter uses `unavailable` for missing CN or fingerprint text. It keeps the
original pointers for `FREEPTR`. Valid text, allocation ownership, certificate
loading, peer verification, and authentication policy remain unchanged.

Run the actual-source host regression with Clang, ASan/UBSan, and Cppcheck:

```sh
CC=/absolute/clang ANALYZER_CC=/absolute/clang CPPCHECK=/absolute/cppcheck \
  ruby ember/tests/syslog-tls-status.rb /absolute/source /new/receipt
```

The optional third argument selects a candidate `tls.c` outside the source tree.
The runner extracts the real status-format statements and the real `FREEPTR`
macro. It runs valid, missing-CN, missing-fingerprint, and both-missing cases.
Each case checks exact output, cleared pointers, one free per allocation, and
absence of frees for string literals. Only the library version and allocation
observer are controlled by the fixture. Temporary trees and executables are removed.

The original source fails three of sixteen output and ownership checks.
The candidate passes all sixteen, including ASan/UBSan. Clang Static Analyzer
and Cppcheck report no findings in the extracted host regression.
This is not a Valgrind run; the tested macOS host is not a supported Valgrind target.

The actual amd64 GCC16 command from the failed world build reproduces the null
`%s` diagnostic at line 303. The same command compiles the candidate. Only source,
output, and the original local include directory change for the isolated copy.
Full-TU GCC `-fanalyzer` reports no path-analysis findings in either source.
The separate analysis comparison omits only `-Werror` from both commands so the
original format warning cannot stop path analysis. It keeps every diagnostic.
Seven existing OpenSSL deprecation warnings remain in each source.
The original also retains the expected null-format warning. The strict build
reproduction and candidate compile keep the original `-Werror` policy.

Cppcheck analyzes each full TU after preprocessing with the real target command.
Use `--language=c`: an `.i` file alone can select the wrong language mode.
Ten inherited findings remain unchanged: six `uselessAssignmentPtrArg`, two
`resourceLeak`, and two `nullPointerRedundantCheck`. No category is disabled.
The two file-leak reports occur on failed `fopen` returns in `write_x509files`;
there is no opened handle on those paths, and the second path closes the key file.
The remaining reports need separate owner review. This narrow patch does not
claim that the full source is analyzer-clean.

This check does not perform a TLS handshake, verify a certificate chain, or accept
a complete OS image. It proves safe formatting and unchanged cleanup for the
optional status fields. Native TLS and full-world validation remain separate gates.
