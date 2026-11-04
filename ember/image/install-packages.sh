#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Install a verified local package bundle.
# The image assembler runs --verify on the build host; the ember_packages
# rc.d script runs the installation once on the target.
set -eu
export PATH=/sbin:/usr/sbin:/bin:/usr/bin
export LC_ALL=C
unset LD_LIBRARY_PATH LD_PRELOAD
verify_only=no
if [ "${1-}" = --verify ]; then
	verify_only=yes
	shift
fi
[ "$#" = 1 ] || { echo "Usage: $0 [--verify] PACKAGE_BUNDLE" >&2; exit 2; }
bundle=$1
# set -e does not stop on a failed test before && or ||: check explicitly.
if [ "$verify_only" = no ]; then
	{ [ "$(uname -s)" = NetBSD ] && [ "$(id -u)" = 0 ]; } || {
		echo 'Install the bundle as root on the target.' >&2; exit 2
	}
fi
cd "$bundle"
for file in packages.sha256 ports-revision rc.d.list; do
	{ [ -f "$file" ] && [ ! -L "$file" ]; } || {
		echo "Missing bundle file: $file" >&2; exit 1
	}
done
[ -s packages.sha256 ]
awk 'NR != 1 || length($0) != 40 || $0 !~ /^[0-9a-f]+$/ { bad = 1 }
    END { if (bad || NR != 1) exit 1 }' ports-revision
# No paths, command fragments or duplicate archive names in the manifest.
manifest_count=$(awk '
NF != 2 || length($1) != 64 || $1 !~ /^[0-9a-f]+$/ ||
    $2 !~ /^[A-Za-z0-9][A-Za-z0-9_.+-]*[.]tgz$/ || seen[$2]++ { bad = 1 }
END { if (bad || NR == 0) exit 1; print NR }
' packages.sha256)
awk 'NF != 1 || $1 !~ /^[a-z0-9_]+$/ || seen[$1]++ { bad = 1 }
    END { if (bad) exit 1 }' rc.d.list

package_sha256()
{
	if command -v sha256 >/dev/null 2>&1; then
		sha256 -q "$1"
	else
		# Permit the same read-only preflight on the build host.
		shasum -a 256 "$1" | awk '{print $1}'
	fi
}

while read -r hash archive || [ -n "$hash$archive" ]; do
	{ [ -f "$archive" ] && [ ! -L "$archive" ]; } || {
		echo "Missing package archive: $archive" >&2; exit 1
	}
	[ "$(package_sha256 "$archive")" = "$hash" ] || {
		echo "Package checksum mismatch: $archive" >&2; exit 1
	}
done < packages.sha256
# pkg_add must never see an unverified archive.
archive_count=0
for entry in ./* ./.[!.]* ./..?*; do
	[ -e "$entry" ] || [ -L "$entry" ] || continue
	[ -f "$entry" ] && [ ! -L "$entry" ] || {
		echo "Non-regular package bundle entry: $entry" >&2; exit 1
	}
	case "${entry#./}" in
	packages.sha256|ports-revision|rc.d.list|installed-packages.sha256|\
	installed-packages.sha256.new) ;;
	*.tgz) archive_count=$((archive_count + 1)) ;;
	*) echo "Unexpected package bundle entry: $entry" >&2; exit 1 ;;
	esac
done
[ "$archive_count" = "$manifest_count" ] || {
	echo 'Package archives do not match the checksum manifest.' >&2; exit 1
}
if [ "$verify_only" = yes ]; then
	echo 'Package bundle checksums and complete inventory verified.'
	exit 0
fi
# A local-only repository: missing dependencies fail instead of fetching.
set --
while read -r hash archive || [ -n "$hash$archive" ]; do
	set -- "$@" "$PWD/$archive"
done < packages.sha256
PKG_PATH=$PWD PKG_RCD_SCRIPTS=YES /usr/sbin/pkg_add -U "$@"
while read -r hash archive || [ -n "$hash$archive" ]; do
	pkg=${archive%.tgz}
	/usr/sbin/pkg_info -e "$pkg" >/dev/null
	/usr/sbin/pkg_admin check "$pkg"
done < packages.sha256
# The image placed these scripts before the first boot. They must be the
# ones the installed packages provide.
while read -r rcd; do
	cmp "/usr/pkg/share/examples/rc.d/$rcd" "/etc/rc.d/$rcd" || {
		echo "rc.d script differs from its package: $rcd" >&2; exit 1
	}
done < rc.d.list
cp packages.sha256 installed-packages.sha256.new
mv -f installed-packages.sha256.new installed-packages.sha256
echo 'Package bundle installed and verified.'
