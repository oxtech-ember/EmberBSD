# Installer runtime bootstrap

An external GCC runtime image must install its runtime before it can execute
programs from the new root. The base `tar`, shell and pkgtools depend on that
runtime. An earlier chrooted extraction failed before package installation.

The installer obtains the bundle and verifies its outer SHA512 before live tar runs.
It retains the preflight freeze and target-copy check.
It now runs the live tar and live runtime helper. The helper's explicit
`--root` mode checks a canonical absolute root and output directory components.
It rejects symbolic-link directories and accepts only data-only package metadata.
The fixed package name, profile, prefix, payload paths and checksum still apply.

`pkg_add -I` skips `+INSTALL`; it does not skip PLIST `@exec` commands.
The metadata gate therefore rejects scripts, executable PLIST directives,
package dependencies and alternate prefixes before invoking pkg_add.
The normal import uses `-I -P ROOT -p /usr/pkg -K /usr/pkg/pkgdb`.
Pkgtools adds ROOT to both the payload prefix and package database.
No manual package database construction or loader environment override is used.
The helper without arguments retains normal installed-system behavior.

Run the causal host checks:

```sh
ruby ember/tests/installer-runtime-altroot.rb
ruby ember/tests/installer-runtime-helper.rb
ruby ember/tests/installer-desktop.rb
```

The shell regression covers arguments, symbolic-link roots and directories,
unsafe metadata, provider conflicts, import failures and verification failures.
The controller regression executes extracted production functions under
ASan/UBSan. It rejects a target tar/helper before runtime installation.
It permits the target pkg_info only after the live helper succeeds.

A separate amd64 EmberBSD VM execution used a private temporary root.
It contained the genuine fork's pkg_info, loader and complete base DSO closure,
without libgcc_s. Chrooted pkg_info failed with the missing libgcc_s diagnostic.
The live candidate helper imported the verified gcc16-runtime-16.2.0nb1 package.
The same target pkg_info then verified that package and returned its version.
The live system package list was unchanged. No compiler or SDK entered the VM.
This checks the bootstrap contract. A rebuilt release, image and complete fresh
installation remain separate acceptance gates.

The complete wizard TU also compiles with its actual amd64 GCC16 build flags.
Baseline and candidate GCC path analysis produced no diagnostics.
Cppcheck used the corresponding target-preprocessed TUs. Both report the same
existing style and getpwnam portability findings; this patch adds none.
Host ASan/UBSan is not Valgrind or physical-device acceptance.
