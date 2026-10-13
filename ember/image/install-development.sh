#!/bin/sh
# Origin: EmberBSD (AI-assisted), install the pinned development package set.
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
if [ "$verify_only" = no ]; then
	[ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ]
	[ "$(id -u)" = 0 ]
fi
cd "$bundle"
[ -s packages.sha256 ] && [ -s ports-revision ]
for file in packages.sha256 ports-revision; do
	[ -f "$file" ] && [ ! -L "$file" ]
done
awk 'NR != 1 || length($0) != 40 || $0 !~ /^[0-9a-f]+$/ { bad=1 }
    END { if (bad || NR != 1) exit 1 }' ports-revision
# No paths, command fragments or duplicate archive names in the manifest.
manifest_count=$(awk '
NF != 2 || length($1) != 64 || $1 !~ /^[0-9a-f]+$/ ||
    $2 !~ /^[A-Za-z0-9][A-Za-z0-9_.+-]*[.]tgz$/ || seen[$2]++ { bad=1 }
$2 ~ /^gdb-[0-9]/ {
    debuggers++
    if ($2 !~ /^gdb-18[.]1(nb[0-9]+)?[.]tgz$/) bad=1
}
$2 ~ /^gcc16-[0-9]/ {
    compilers++
    if ($2 !~ /^gcc16-16[.]2[.]0(nb[0-9]+)?[.]tgz$/) bad=1
}
$2 ~ /^llvm-[0-9]/ {
    providers++
    if ($2 !~ /^llvm-23[.]1[.]2(nb[0-9]+)?[.]tgz$/) bad=1
}
END {
    if (bad || NR == 0 || debuggers != 1 || compilers != 1 || providers > 1) exit 1
    print NR
}
' packages.sha256)

package_sha256()
{
	if command -v sha256 >/dev/null 2>&1; then
		sha256 -q "$1"
	else
		# Permit the same read-only preflight on the macOS build host.
		shasum -a 256 "$1" | awk '{print $1}'
	fi
}

while read -r hash archive || [ -n "$hash$archive" ]; do
	[ -f "$archive" ] && [ ! -L "$archive" ]
	[ "$(package_sha256 "$archive")" = "$hash" ] || {
		echo "Package checksum mismatch: $archive" >&2; exit 1;
	}
done < packages.sha256
# pkg_add must never see an unverified archive, including hidden files or
# another supported archive suffix. Only our fixed receipt names may coexist.
archive_count=0
for entry in ./* ./.[!.]* ./..?*; do
	[ -e "$entry" ] || [ -L "$entry" ] || continue
	[ -f "$entry" ] && [ ! -L "$entry" ] || {
		echo "Non-regular package bundle entry: $entry" >&2; exit 1;
	}
	case "${entry#./}" in
	packages.sha256|ports-revision|installed-packages.sha256|\
	installed-packages.sha256.new|gdb-version.txt|gdb.sha256|\
	cc-version.txt) ;;
	*.tgz) archive_count=$((archive_count + 1)) ;;
	*) echo "Unexpected package bundle entry: $entry" >&2; exit 1 ;;
	esac
done
[ "$archive_count" = "$manifest_count" ] || {
	echo 'Package archives do not match the checksum manifest.' >&2; exit 1;
}
if [ "$verify_only" = yes ]; then
	echo 'Package bundle checksums and complete inventory verified.'
	exit 0
fi
# A local-only repository: missing dependencies fail instead of fetching.
export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/usr/pkg/sbin:/usr/pkg/bin
set --
while read -r hash archive || [ -n "$hash$archive" ]; do
	set -- "$@" "$PWD/$archive"
done < packages.sha256
PKG_PATH=$PWD /usr/sbin/pkg_add -U "$@"
while read -r hash archive || [ -n "$hash$archive" ]; do
	pkg=${archive%.tgz}
	/usr/sbin/pkg_info -e "$pkg" >/dev/null
	/usr/sbin/pkg_admin check "$pkg"
done < packages.sha256
/usr/pkg/bin/gdb -nx -nh -batch -ex 'show version' > gdb-version.txt
grep -q '^GNU gdb (GDB) 18\.1$' gdb-version.txt
[ -L /usr/bin/gdb ] && [ "$(readlink /usr/bin/gdb)" = /usr/pkg/bin/gdb ]
[ "$(command -v gdb)" = /usr/bin/gdb ]
[ ! -e /usr/bin/gdbtui ] && [ ! -L /usr/bin/gdbtui ]
sha256 -q /usr/pkg/bin/gdb > gdb.sha256
# The system compiler must resolve to the packaged GCC16 on first use.
for tool in cc c++ cpp g++ gcc; do
    [ -L "/usr/bin/$tool" ] && [ "$(readlink "/usr/bin/$tool")" = "/usr/pkg/gcc16/bin/$tool" ]
done
/usr/bin/cc --version > cc-version.txt
head -1 cc-version.txt | grep -q '^cc (GCC) 16\.2\.0$'
printf 'int main(void) { return 0; }\n' > /tmp/ember-cc-smoke.c
/usr/bin/cc /tmp/ember-cc-smoke.c -o /tmp/ember-cc-smoke
/tmp/ember-cc-smoke
printf 'int main() { return 0; }\n' > /tmp/ember-cxx-smoke.cpp
/usr/bin/c++ /tmp/ember-cxx-smoke.cpp -o /tmp/ember-cxx-smoke
/tmp/ember-cxx-smoke
rm -f /tmp/ember-cc-smoke.c /tmp/ember-cc-smoke /tmp/ember-cxx-smoke.cpp /tmp/ember-cxx-smoke
cp packages.sha256 installed-packages.sha256.new
mv -f installed-packages.sha256.new installed-packages.sha256
echo 'EmberBSD development package bundle installed and verified'
