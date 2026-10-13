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
The public CDN is planned. Use your own verified server until that service is available.
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
They do not establish a booted image or physical installation.
Build a clean committed source export, then test the resulting image in a disposable amd64 VM.
Record the image hashes, kernel identity, source failures, review, completion and disk-only boot.
The image build uses the tree's GCC 12.5 bootstrap profile; GCC16 migration is separate.
The general [VM walkthrough](amd64-vm-install.md) describes the surrounding build and boot setup.
