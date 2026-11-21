# Base OpenSSH source selection

The base build uses `EMBER_OPENSSH_SOURCE` to select the source prepared by the
common Ports OpenSSH owner. The native makefiles provide target configuration,
base integration and installation; the common source supplies upstream code and
Ports patches. `obj` and other metadata-only operations do not require a source.
The NetBSD-vendored `getexecpath.c` is selected only with that vendored tree;
the common portable release uses its configured function check and upstream
fallback. Its source list must resolve entirely within the selected make paths.

The selected release intentionally changes the private `libssh` ABI from
major 50 to 51. Consumers must be rebuilt against major 51; no major-50 alias
is provided. The previous private exports `cipher_set_keyiv`,
`crypto_sign_ed25519`, `crypto_sign_ed25519_open`,
`crypto_sign_ed25519_ref_ge25519_base`,
`crypto_sign_ed25519_ref_isneutral_vartime`,
`crypto_sign_ed25519_ref_unpackneg_vartime`, `ssh_getpass`, `ssh_getpeereid`,
`ssh_packet_authentication_state`, and `sshpkt_sendx` are no longer exported.
The selected symbol inventory is shared across dynamic targets; current build
and symbol validation has been performed on AArch64. Static platforms retain
their native selection and are not covered by that dynamic-library result.

For a prepared source, each native configuration checks its target libraries,
configure inputs and selected tools. GNU configure writes `build/config.h`.
After a successful check, the helper publishes `config.h` in the configuration
object directory for C compilation and dependency generation. Reconfiguring
that role with identical header bytes preserves the consumer header's timestamp.
A changed header invalidates its consumers. Programs with and without Kerberos
and the private library keep their existing configuration selection.

When `EMBER_OPENSSH_SOURCE` changes to a different prepared directory in an
existing object tree, generated `.depend` and compiler `.d` files may still
name the old absolute source path. This change is not automatically
invalidated. Run the selected NetBSD make wrapper's `cleandir` target from
`crypto/external/bsd/openssh`, then rebuild the selected OpenSSH components.
This cleans the OpenSSH object subtrees; it does not clean the whole userland
or installed `DESTDIR`.

OpenSSH 10.6p1 still uses OpenSSL's legacy `EC_KEY`/ECDH APIs in both
`kexecdh.c` and `kexmlkem768ecdh.c`. The native build keeps deprecation
warnings visible for these paths without turning them into build failures.

`EMBER_OPENSSH_CRYPTO=openssl|builtin` selects the prepared source's crypto
backend; when unset, the historical default remains OpenSSL except on m68000.
The explicit `builtin` selection is currently supported only with `MKPIC=no`:
the prepared-source export inventory is OpenSSL-specific, so dynamic/PIC
built-in builds are rejected until a separate built-in ABI inventory is
maintained. The small static diagnostic profile uses this supported boundary.
Run `tests/sha2-adapter.py` with the selected native Ports Clang and Cppcheck
to check the actual adapter against the libc SHA2 implementation using
known-answer vectors, static analysis, and ASan/UBSan.

With LDAP enabled, the listener parses the same LPK configuration as the
privileged authentication process; LDAP key lookup remains in the monitor.
The native HPN configuration remains available. The historical NONE switch does
not establish an interoperable cipher transition; see the common Ports tests
for its precise compatibility boundary.
