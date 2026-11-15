# OpenSSL in the base system

The base libraries and `openssl` program use the native OS make targets. Set
`EMBER_OPENSSL_SOURCE` to the prepared `security/openssl` source from the
common Ports recipe. The SDK verifies that input before an OS build. The
prepared tree is read-only; upstream Configure, generated headers, assembly,
source lists and symbol maps live under the OpenSSL OS object directory.

The parent target prepares these files before recursive `includes`, `depend`,
`all` and `install`. The build selects `HOST_PYTHON`, `HOST_PERL`, `HOST_GMAKE`
and `HOST_SH` from the native tool prefix, and selects the target compiler and
sysroot separately. The caller keeps the ordinary object-tree writer lease for
the entire recursive build. The generator reads the prepared-source receipt and
the existing SDK object context in its ancestor object tree. That context
identifies the OS generator code, compiler, native tools, target ABI and sysroot
configuration. Matching [input keys](https://github.com/oxtech-ember/EmberBSD-SDK/blob/main/docs/input-keys.md)
reuse the generated output after checking that required output files exist,
without reading source trees, headers, executables or generated contents again.
Direct invocations without these identities generate afresh. Unchanged generated file timestamps survive regeneration for a new key.

The generated `crypto/buildinf.h` displays the selected compiler family,
numeric version, target triplet and OpenSSL platform. Like the native NetBSD
libcrypto Makefile, it omits host toolchain paths and flags from the installed
library. Its build date is explicitly not recorded, so regeneration never embeds
wall-clock time. Configure and compilation still use the full selected compiler
command and flags. The private `generated/.complete` receipt retains upstream's
original build-info header, including all configured compiler flags, alongside
the complete caller arguments in its input key; it is not installed.

The OpenSSL 4 libraries use base ABI major 17. Their symbol version is supplied
by upstream OpenSSL, independently of the base shared-library major. AArch64 library and program cross-builds have been exercised; target runtime
and other architectures require their own checks.
