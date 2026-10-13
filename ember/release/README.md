# Prepare and publish an installation kit

This procedure builds one pinned amd64 UEFI candidate and publishes its exact files through GitHub Releases.
It uses the product version `1.0` and the NetBSD `11.0` compatibility base.
GitHub Pages hosts the project site and download navigation.
The binary files belong to a release, not the Pages repository.

## Build one complete candidate

Read [BUILDING](../../BUILDING) and the [VM installation guide](../boot/amd64-vm-install.md).
The host needs C/C++ build tools, Git, Ruby, gzip and an ISO-capable libarchive `tar`.
Use a case-sensitive build volume and unused absolute paths without whitespace.
Reserve space for the full source export, host tools, objects, destination and images.
Keep your own build directories separate from another contributor's work.

From the source repository, select a full accepted source commit:

```sh
git fetch origin
sh ember/release/build-amd64.sh FULL_SOURCE_COMMIT /absolute/new-build
```

The wrapper exports the committed tree.
It does not use uncommitted files or an existing output directory.
The upstream `-u` option avoids discarding tools built earlier in this same invocation.
The source, object and destination directories still start empty.
It builds `tools`, `release`, `install-image` and `iso-image`.
The profile sets `MKX11=no`, `MKRUMP=no`, `MKGCCCMDS=no`, `MKREPRO=yes`,
`KERNEL_SETS=GENERIC` and `BUILD_KERNELS=INSTALL`.
These two values also enter `MAKEFLAGS` as command variables to override the machine defaults.
`EMBER_BUILD_ID` and kernel `BUILDINFO` contain the same full source commit.
`EMBER_RELEASE_JOBS` selects the worker count; its default is four.
The wrapper raises the host process stack limit to at least 65520 KiB.
This prevents bootstrap GCC/MPFR stack exhaustion on a Darwin host with the default 8 MiB limit.
It preserves an existing larger limit and stops if the host cannot raise a smaller limit.
The successful build writes `build-success`. A failed build preserves its log and remains unqualified.

The in-tree GCC12.5 compiler is a host bootstrap.
Native GCC12 commands are omitted from the target sets.
Modern native development tools belong to the
[Ports development profile](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain).
This installer profile does not establish a complete GCC16 userland or a desktop image.

## Prepare the exact files

```sh
ruby ember/release/release.rb prepare /absolute/new-build /absolute/new-kit
ruby ember/release/release.rb verify /absolute/new-kit
```

Use a new kit directory each time.
Preparation takes sets from the exact full ISO, not a shared release directory.
It checks their embedded SHA512 values.
It checks the installed kernel's full build identity and the ISO boot kernel's hash.
It checks the USB image's gzip integrity.
The kit contains the full ISO, compressed UEFI USB image, matching binary sets,
`manifest.json`, `SHA256`, the original set `SHA512` and release notes.
The manifest records the profile, compiler, source archive hash, component IDs, file sizes and file hashes.
It names optional absent sets explicitly.
Personal VM disks, keys and unrelated files are excluded.

Local verification rejects missing files, extra files, symlinks, changed bytes, mixed component IDs
and files which meet or exceed GitHub's 2 GiB per-asset limit.
Hashes detect changed files. The build and VM records establish the stated provenance and tested scope.
A locally prepared candidate is not a published or accepted release.

## Qualify the candidate in a disposable VM

Use a new guest disk and firmware variables.
Do not attach physical host storage or another VM's writable disk.
Run the [first boot and reboot checks](../boot/amd64-vm-install.md#boot-the-installed-disk).
Install from the USB image and check the installed kernel against the kit.
Run a fresh HTTPS installation of the complete selected profile from the exact candidate sets.
Check a GitHub release redirect with the same guest client.
Check that an unknown CA, a wrong certificate name, missing trust anchors and HTTP 404 cause rejection.
Record the image hashes, installed kernel hash and observed results.
Do not treat a host compile, a menu fixture or an unrelated guest as installation acceptance.

The publication receipt has the following keys.
Replace the identity placeholders with the observed values.
Set each check to `true` only after its actual test passes:

```json
{
  "source_commit": "FULL_SOURCE_COMMIT",
  "image_sha256": "FULL_ISO_SHA256",
  "usb_image_sha256": "FULL_USB_IMAGE_SHA256",
  "installed_kernel_sha256": "INSTALLED_NETBSD_SHA256",
  "checks": {
    "local_install": false,
    "https_install": false,
    "first_boot_without_media": false,
    "reboot_persistence": false,
    "tls_reject_unknown_ca": false,
    "tls_reject_wrong_name": false,
    "tls_reject_missing_anchors": false,
    "http_404_rejected": false,
    "github_redirect": false
  }
}
```

Keep passwords, host paths and private certificate keys outside the receipt.
Create another complete kit with the measured receipt:

```sh
ruby ember/release/release.rb prepare /absolute/new-build /absolute/qualified-kit /absolute/vm-receipt.json
ruby ember/release/release.rb verify /absolute/qualified-kit
```

Qualification must match both image hashes, the source commit and installed kernel.
The publisher refuses incomplete qualification.
The receipt is a tester's record. The tool checks its binding; it does not conduct those VM tests for the tester.

## Publish through GitHub Releases

Use a GitHub CLI session with the required repository rights.
Keep credentials in the client's supported credential store or environment.
Do not pass tokens to this tool.
Integrate and push the reviewed source to the repository's accepted `main` first.
The publisher checks that public `main` contains the candidate's source.
Follow [CONTRIBUTING](../../CONTRIBUTING.md) and the repository's protection rules.

Enable immutable releases once, with repository administration rights:

```sh
gh api --hostname github.com --method PUT repos/oxtech-ember/EmberBSD/immutable-releases
gh api --hostname github.com repos/oxtech-ember/EmberBSD/immutable-releases
```

The second command must report `enabled: true`.
See [GitHub release immutability](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases).

```sh
ruby ember/release/release.rb draft /absolute/qualified-kit
ruby ember/release/release.rb publish /absolute/qualified-kit
ruby ember/release/release.rb verify-remote /absolute/qualified-kit
```

`draft` creates a private prerelease from the exact source commit.
Its tag is `build-<full-source-commit>-amd64-no-x11`.
It uploads only the verified kit files, then downloads and checks every file.
An interrupted draft can resume with identical existing assets.
Different bytes, foreign tags, extra assets and published releases prevent asset writes.
The tool never uses `--clobber`.

`publish` requires VM qualification and repeats the remote byte checks.
It opens the draft as an immutable prerelease and leaves `latest` unchanged.
It verifies GitHub's release attestation and every local asset.
If publication succeeds but the attestation check is unavailable, keep the actual published state visible.
Use `verify-remote` to retry verification. Do not delete or replace the release.

After publication, install onto another empty VM disk from the baked GitHub HTTPS source.
Check the installed kernel and complete set profile again.
Check an external download, SHA256, Range/resume and a missing release's HTTP 404.
Record this GitHub-native acceptance separately from the local HTTPS fixture.
Only then add the accepted candidate to the public download navigation.
Physical computers and AArch64 boards need their own acceptance.

## Source paths and a later domain

The installation-media path stays `/amd64/binary/sets`.
The network path is independent. The release wrapper sets:

```text
Host: github.com
Base directory: oxtech-ember/EmberBSD/releases/download/build-<full-source-commit>-amd64-no-x11
Binary set directory: empty
```

The source menu permits an operator to change these values explicitly.
`SYSINST_FTP_SET_DIR_BIN` changes the compiled network binary prefix only.
The sysinst configuration keys `ftp prefix` and `local prefix` are independent.
The `ftp prefix` key now affects network OS sets; use `local prefix` for installation media.
Source sets and package source paths retain their separate behavior.
The binary channel does not provide sysinst source archives.

The amd64 media contain a ready CA bundle from the pinned Mozilla `server.trust` list.
The minimal FTP client enables OpenSSL through `SMALLPROG_HTTPS`.
Missing trust anchors never enable an automatic unverified HTTPS mode.
HTTP remains an explicit operator choice where the image offers it.

A custom download domain needs a separately accepted HTTPS frontend and URL mapping.
A DNS CNAME alone does not configure GitHub Releases to serve that custom hostname.
Keep immutable release URLs and update site navigation separately.
For withdrawal, remove the candidate from navigation and state the reason in release notes.
Do not replace its assets or tag.

## Source checks

```sh
ruby ember/tests/installer-https.rb
ruby ember/tests/installer-memory.rb
ruby ember/tests/release-build.rb
ruby ember/tests/release-kit.rb
ruby ember/tests/release-github.rb
```

The source contracts execute the trust policy, set selection and allocation handling.
Host C probes use ASan/UBSan.
The build wrapper test checks orchestration, not an OS build.
The GitHub fixture checks refusal behavior without changing a repository.
Use the actual image regression and VM checks for each built candidate.
