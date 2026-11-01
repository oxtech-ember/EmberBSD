# Base OpenSSH source selection

The base build uses `EMBER_OPENSSH_SOURCE` to select the source prepared by the
common Ports OpenSSH owner. The native makefiles provide target configuration,
base integration and installation; the common source supplies upstream code and
Ports patches. `obj` and other metadata-only operations do not require a source.

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

OpenSSH 10.6p1 still uses OpenSSL's legacy `EC_KEY`/ECDH APIs in both
`kexecdh.c` and `kexmlkem768ecdh.c`. The native build keeps deprecation
warnings visible for these paths without turning them into build failures.

With LDAP enabled, the listener parses the same LPK configuration as the
privileged authentication process; LDAP key lookup remains in the monitor.
The native HPN configuration remains available. The historical NONE switch does
not establish an interoperable cipher transition; see the common Ports tests
for its precise compatibility boundary.
