<!-- Origin: EmberBSD guided text installation, 2026-10-13. -->
# EmberBSD text installer

The amd64 installer provides a six-step keyboard wizard on an 80 by 25 console.
It uses the shared flame mascot, a dark background and orange highlights.
The guided interface is English. The existing expert menus retain their translated catalogs.
Smaller consoles and other architectures use the expert interface.

The boot menu waits 10 seconds. Its first entry starts quietly with the -z boot flag.
Choose diagnostic output to enable verbose and debug messages with -vx.
Installed x86 systems provide the same choices, plus single-user mode and the boot prompt.
Kernel messages remain available through dmesg. Service startup output is saved in /var/run/rc.log.

## First login

The console identifies the product as EmberBSD. The login greeting uses
the EmberBSD version from /etc/release rather than the kernel compatibility
version. Multi-line copyright years are not printed at each login; attribution
remains in /etc/release and the source licenses. Local motd notices survive
automatic greeting updates. Diagnostic tools retain their upstream ABI names.

SSH is enabled by default; its authentication defaults are unchanged.
The System step has a Graphics menu: None, XFCE with a Wi-Fi manager,
and a disabled KDE Plasma entry marked as in development. None is the default.
Selecting XFCE also selects the X11 sets, independently of the base profile.
The desktop and its Wi-Fi manager are installed together with the OS.
Run ember-desktop after console login to start the installed XFCE session.

XFCE requires desktop-xfce.tar.xz in the same set directory, with its
SHA512 entry in the build manifest. Sysinst freezes and verifies the complete
bundle before allowing disk writes, and verifies the target copy again before
running package installation. Small ISO miniroots use a private, size-limited
RAM filesystem for staging. Missing or damaged bundles return to Source.
Package installation errors prevent a successful completion message.
The initial amd64 bundle uses pinned upstream pkgsrc binaries; the GTK3 Wi-Fi
manager is built from pinned original dhcpcd-ui sources. Package provenance
is separate from the EmberBSD kernel and base build identity.

The console variant provides wpa_cli and wpa_supplicant. Administrators can
configure /etc/wpa_supplicant.conf, enable wpa_supplicant and dhcpcd in
/etc/rc.conf, and use wpa_cli to scan and connect. Keep that configuration
mode 600. The graphical variant enables those services and the Wi-Fi control
sockets for the administrative wheel group. It discovers radios independently
of the Ethernet interface used during installation. Saved static addresses remain authoritative. Actual radio operation requires
a supported adapter; an Ethernet-only VM cannot establish wireless acceptance.

## Install from a complete image

1. Boot the complete EmberBSD USB image.
2. Press Enter at the mascot. Select the language and keyboard layout.
3. Identify the target disk by its device, model and capacity.
4. Review the automatic GPT layout on the disk step. It replaces the entire target layout.
5. Select the installation image as the set source.
6. Select Standard, Minimal, or Custom. Choose optional XFCE and Wi-Fi in System.
7. Set the hostname and timezone. A local account is optional.
8. Read the disk, partitions, source and build ID on the review screen.
9. Press I to write and install. Set passwords when prompted after copying the sets.
10. Finish installation. Remove the installation medium before starting the installed system.

Use the arrow keys to select an item. Enter continues. Esc returns to the previous step.
Use Ctrl-U to clear an input field. Invalid names keep the field open with an explanation.
The boot USB disk is excluded from the guided target list.
The automatic plan does not change disk metadata before the final I confirmation.
Cancellation and source checks before that confirmation preserve the existing layout.
Confirmation requires at least 80 columns and 25 rows so that the warning remains visible.

Standard installs available binary sets. It excludes X11 when Graphics is None.
Minimal selects the bootable base system. XFCE adds the required X11 sets to either profile.
Custom allows binary-set selection. Source archives use the separate expert interface.
Default profiles can omit absent optional compatibility sets; the review shows their count.
A missing set explicitly selected in Custom fails the source check.
The wizard offers five timezones. Use advanced settings for other system configuration.
Creating an optional local account opens its password dialog after copying the sets.
Use the completion screen's Advanced settings to configure the root password.
Automatic whole-disk GPT is the guided layout. Press A at the welcome screen for manual layouts,
upgrades, recovery and the inherited expert tools with their own confirmations.

## Console appearance

The palette uses background #101110, orange #FFAB42, foreground #EEE8DE,
muted text #9F9A90, borders #43443D, red #FF463B and yellow #FFE466.
The amd64 UEFI welcome page applies the same palette on standard 32-bit RGB or
BGR GOP framebuffers. Firmware continues to draw the text and handle input.
Unsupported pixel formats and serial consoles retain firmware colors.

The installer requests the RGB palette capability from the framebuffer console.
It saves and restores the previous palette and font when leaving the wizard.
Spleen 12 by 24 or 16 by 32 makes the 80 by 25 grid legible when the display fits;
adding these existing fonts to the amd64 kernel can also change its default font.
Unsupported, indexed or glyph-cache consoles use the terminal color fallback.
The six steps share a header, sidebar, wide selection highlight and keyboard footer.
Legacy expert tools keep their existing dialogs.

## Install from a network source

Select the CDN/network source and provide the immutable directory for the boot image's build.
The default channel is described in [installer identity](installer-branding.md#distribution-sets).
Published candidates supply matching sets through GitHub HTTPS. The custom CDN domain
is separate. Use a verified set server until your newly built candidate is published.
The directory must contain BUILDID, SHA512 and all selected binary sets.
BUILDID must contain the full EMBER_BUILD_ID used to build the boot image.
SHA512 uses the release builder's SHA512 (filename) = digest format.
Publish compatible kernel, module and base sets together. Use a new directory for every build.
A patched kernel requires a matching boot image, even when the set server changes quickly.

Configure a network interface through the existing network dialog.
HTTPS requires the installation environment's TLS support and trusted certificates.
HTTP has no transport authentication. Use it only on a trusted installation network.
The preflight checks the build ID and selected checksum entries before disk writes.
It downloads metadata, not all archives. A later transfer failure can occur after formatting.
The installer checks each downloaded archive's SHA512 before extracting it.
On failure, select Retry, another source, or the recent log.
Retries retain the profile and selected sets and do not repeat partitioning.
The detailed installer log is /tmp/sysinst.log in the installation environment.

## Verification

The checks in ember/tests/installer-wizard* exercise actual controller and adapter bodies,
the shared renderer, source validation and GPT replacement with the host-built GPT tool.
The executable contracts use ASan and UBSan on supported hosts.
Run `CFLAGS='-fsanitize=address,undefined' ruby ember/tests/installer-keyboard.rb`
to check the actual keyboard-hook declarations with and without WSKBD.
The contract checks conditional statements, condition evaluation and hook calls.
Set `CC` and `CFLAGS` for a cross compiler, and add `--compile-only` for target objects.
The evbarm/aarch64 main translation unit also passes GCC16 with `-Werror` and `-fanalyzer`.
They do not establish a booted image or physical installation.
Build a clean committed source export, then test the resulting image in a disposable amd64 VM.
Record the image hashes, kernel identity, source failures, review, completion and disk-only boot.
The image build uses the tree's GCC 12.5 bootstrap profile; GCC16 migration is separate.
The general [VM walkthrough](amd64-vm-install.md) describes the surrounding build and boot setup.

The complete candidate at `56046a492ea498bfe2a38ae635a60bba7551c574` passes amd64 UEFI VM acceptance.
Standard/XFCE installs from the full USB image and starts an installed XFCE session after cold boot.
Thunar, the terminal and the automatically started dhcpcd-gtk preferences open visibly.
Minimal/None installs five required sets over an explicit trusted HTTP source and cold-boots without media.
Both installations pass the installed kernel hash, running SSH handshake and console Wi-Fi tool checks.
An HTTP 404 and a damaged desktop bundle return to Source before confirmation.
The complete 32 GiB target disk still matches its saved baseline after those failures.
The repaired source retains the profile and network settings.
These checks do not qualify public HTTPS delivery of this desktop kit, BIOS installation, physical radios or GPU acceleration.
The separate published no-X11 kit has its own HTTPS acceptance record.

## Build a desktop candidate

The no-X11 release wrapper is a separate profile. To build the desktop media,
export a clean OS commit and use its own X11 makefiles. Obtain the original
[NetBSD 11.0 xsrc.tgz](https://cdn.netbsd.org/pub/NetBSD/NetBSD-11.0/source/sets/xsrc.tgz)
from the upstream source sets and verify its SHA512:

```text
c46c9e62d225732b3d8d30d2c5f95b992b434826276f8b66fc04439a309b13e4b751d33366f5c39010f2e309ad70a7bdc7a130fe5ea735792be26ea7c13e2dd5
```

Prepare the frozen bundle through the
[Ports desktop recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/installer-desktop).
Keep package source and archive hashes with the candidate. They do not replace
the OS build identity. Use an unused absolute build directory without spaces:

```sh
set -e
revision=FULL_OS_COMMIT
out=/absolute/new-desktop-build
timestamp=RECORDED_REPRO_TIMESTAMP
mkdir "$out" "$out/source" "$out/x11"
git archive -o "$out/source.tar" "$revision"
tar -xf "$out/source.tar" -C "$out/source"
tar -xzf /absolute/verified-xsrc.tgz -C "$out/x11"
cd "$out/source"
export MAKECONF=/dev/null
unset EXTERNAL_TOOLCHAIN MAKEFLAGS MFLAGS MAKEOBJDIR MAKEOBJDIRPREFIX
unset NETBSDOBJDIR NETBSDOBJROOT
set -- -U -u -m amd64 -j4 -O "$out/obj" -T "$out/tools" \
    -D "$out/dest" -R "$out/release" \
    -V MKRUMP=no -V MKX11=yes -V MKGCCCMDS=no \
    -V "X11SRCDIR=$out/x11/usr/xsrc" \
    -V KERNEL_SETS=GENERIC -V BUILD_KERNELS=INSTALL \
    -V 'MAKEFLAGS=KERNEL_SETS=GENERIC BUILD_KERNELS=INSTALL' \
    -V "EMBER_BUILD_ID=$revision" -V "BUILDINFO=EmberBSD:$revision" \
    -V MKREPRO=yes -V "MKREPRO_TIMESTAMP=$timestamp"
sh build.sh "$@" tools kernel=GENERIC kernel=INSTALL release
```

Stop if export, extraction or compilation fails. Record the complete arguments
and successful log. The timestamp is a fixed input, not the current wall clock.
Copy the verified desktop-xfce.tar.xz beside the resulting binary sets. Rebuild
their SHA512 manifest with all actual set archives and the desktop bundle.
Keep the generated BUILDID unchanged. Then run the same pinned build arguments
with install-image and iso-image. Do not rebuild release after adding the bundle
without regenerating its manifest. Test both the desktop and console profiles
from the resulting media in separate disposable guests.

Network defaults can be supplied through SYSINST_HTTP_HOST, SYSINST_FTP_DIR and
SYSINST_FTP_SET_DIR_BIN. An empty binary prefix selects a flat release directory.
The direct wizard path honors these values and retains a custom URL on retry.
Use a directory containing this candidate's own BUILDID, sets and bundle.
An older published kit cannot supply matching sets for a newly compiled boot image.

On macOS, the host font tools use system zlib and Expat's host endian/random configuration.
Run `ruby ember/tests/x11-host-freetype.rb` and `ruby ember/tests/x11-host-expat.rb` for those actual-source regressions.
The accepted release retains inherited SiS manual preprocessor diagnostics; its generated manual renders successfully.
The native Wi-Fi leaf can report absent pkg_install version metadata; installation and runtime checks still pass.
Neither diagnostic is suppressed or presented as a clean full warning log.
