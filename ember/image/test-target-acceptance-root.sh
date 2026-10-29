#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise root assembly with fixture tools and optionally selected OS tools.
set -eu

host_sh=${HOST_SH:-}
case $host_sh in
	/*) [ -x "$host_sh" ] || { echo "selected HOST_SH is not executable" >&2; exit 2; } ;;
	*) echo "HOST_SH must be the explicitly selected SDK shell" >&2; exit 2;;
esac
host_awk=${HOST_AWK:-}
case $host_awk in
	/*) [ -x "$host_awk" ] || { echo "selected HOST_AWK is not executable" >&2; exit 2; } ;;
	*) echo "HOST_AWK must be the explicitly selected SDK awk" >&2; exit 2;;
esac
export HOST_AWK
case ${PYTHON:-} in
	/*) [ -x "$PYTHON" ] || { echo "selected PYTHON is not executable" >&2; exit 2; } ;;
	*) echo "PYTHON must be the explicitly selected SDK Python" >&2; exit 2;;
esac

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
helper=$here/target-acceptance-root.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/target-acceptance-root.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/root/etc" "$work/root/dev" "$work/root/tests" \
	"$work/dest/etc" "$work/dest/dev" "$work/os/etc" "$work/tools/bin"
cp "$here/target-acceptance-rc" "$work/root/etc/rc"
chmod 755 "$work/root/etc/rc"
cmp "$here/target-acceptance-rc" "$work/root/etc/rc"
grep -Fq '/sbin/halt -p -q' "$work/root/etc/rc" || {
	echo 'diagnostic rc must use quick poweroff without rc shutdown' >&2
	exit 1
}
if grep -Fq '/sbin/shutdown' "$work/root/etc/rc"; then
	echo 'diagnostic rc must not enter the full shutdown path' >&2
	exit 1
fi
printf 'nonce-test\n' > "$work/root/etc/emberbsd-target-acceptance.nonce"
printf 'ld0a\n' > "$work/root/etc/emberbsd-target-acceptance-root-device"
printf '#!/bin/sh\nexit 0\n' > "$work/root/tests/run-target.sh"
cat > "$work/dest/dev/MAKEDEV" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
	case $1 in
	std)
		printf './console type=char mode=600\n./null type=char mode=666\n'
		printf './tty type=char mode=666\n./zero type=char mode=666\n'
		;;
	ld0)
		printf './ld0a type=block mode=640\n./rld0a type=char mode=640\n'
		;;
	esac
	shift
done
EOF
printf '#!%s\n' "$host_sh" > "$work/tools/bin/nbmtree"
cat >> "$work/tools/bin/nbmtree" <<'EOF'
if [ "${OMIT_SET:-no}" = yes ]; then
cat <<SPEC
./etc type=dir
./dev type=dir
SPEC
exit 0
fi
cat <<SPEC
/set type=dir mode=755 uid=501 gid=20
. type=dir
./etc type=dir
./bin type=dir
./dev type=dir
./tests type=dir
./etc/rc type=file mode=755
SPEC
EOF
printf '#!%s\n' "$host_sh" > "$work/tools/bin/nbmakefs"
cat >> "$work/tools/bin/nbmakefs" <<'EOF'
set -eu
spec=
previous=
last=
want_spec=no
for arg do
	if [ "$want_spec" = yes ]; then
		spec=$arg
		want_spec=no
	fi
	[ "$arg" != -F ] || want_spec=yes
	previous=$last
	last=$arg
done
[ -n "$spec" ] && [ -f "$spec" ]
grep -q '^./dev/ld0a type=block mode=640' "$spec"
for device in console null tty zero; do
	grep -q "^./dev/$device type=char" "$spec" || {
		echo "missing standard boot device $device" >&2
		exit 1
	}
done
"$HOST_AWK" '
	$1 == "/set" {
		seen++
		if (seen == 1) {
			if ($0 !~ /(^|[[:space:]])uid=0([[:space:]]|$)/ ||
			    $0 !~ /(^|[[:space:]])gid=0([[:space:]]|$)/)
				bad=1
		} else if ($0 ~ /(^|[[:space:]])(uid|gid)=/) {
			bad=1
		}
	}
	END { exit !(seen > 0 && !bad) }
' "$spec" || { echo 'FFS spec has non-root owner defaults' >&2; exit 1; }
if grep -q 'uid=501\|gid=20' "$spec"; then
	echo 'FFS spec retained host ownership' >&2
	exit 1
fi
image=$previous
[ ! -e "$image" ]
printf 'fixture only\n' > "$image"
EOF
chmod 755 "$work/tools/bin/nbmtree" "$work/tools/bin/nbmakefs" "$work/dest/dev/MAKEDEV"

"$host_sh" "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ld0a "$work/root.ffs" "$host_sh"
[ "$(cat "$work/root.ffs")" = 'fixture only' ]
mv "$work/root/etc/rc" "$work/root/etc/rc.saved"
if "$host_sh" "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ld0a "$work/no-runcom.ffs" "$host_sh" >/dev/null 2>&1; then
	echo 'root without /etc/rc unexpectedly passed' >&2
	exit 1
fi
[ ! -e "$work/no-runcom.ffs" ]
mv "$work/root/etc/rc.saved" "$work/root/etc/rc"
cat > "$work/bad-owner.spec" <<'EOF'
/set uid=501 gid=20
./dev/ld0a type=block mode=640
EOF
if "$work/tools/bin/nbmakefs" -F "$work/bad-owner.spec" \
	"$work/bad-owner.ffs" "$work/root" >/dev/null 2>&1; then
	echo 'bad host uid/gid fixture unexpectedly passed' >&2
	exit 1
fi
[ ! -e "$work/bad-owner.ffs" ]

if "$host_sh" "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ldXa "$work/invalid.ffs" "$host_sh" >/dev/null 2>&1; then
	echo 'invalid root device unexpectedly passed' >&2
	exit 1
fi
[ ! -e "$work/invalid.ffs" ]
if OMIT_SET=yes "$host_sh" "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ld0a "$work/no-mtree-default.ffs" "$host_sh" >/dev/null 2>&1; then
	[ "$(cat "$work/no-mtree-default.ffs")" = 'fixture only' ]
else
	echo 'spec without an nbmtree /set default was rejected' >&2
	exit 1
fi
if [ -n "${EMBER_OS_TOOLDIR:-}" ] || [ -n "${EMBER_OS_DESTDIR:-}" ] || \
	[ -n "${EMBER_OS_SOURCE:-}" ] || [ -n "${EMBER_ROOT_DEVICE:-}" ]; then
	: "${EMBER_OS_TOOLDIR:?set same-build TOOLDIR}"
	: "${EMBER_OS_SOURCE:?set matching EmberBSD source tree}"
	: "${EMBER_ROOT_DEVICE:?set an explicit ld partition, e.g. ld0a}"
	actual_dest=${EMBER_OS_DESTDIR:-$work/actual-dest}
	if [ -z "${EMBER_OS_DESTDIR:-}" ]; then
		mkdir -p "$actual_dest/etc" "$actual_dest/dev"
		MACHINE=aarch64
		MACHINE_ARCH=aarch64
		NETBSDSRCDIR=$EMBER_OS_SOURCE
		export MACHINE MACHINE_ARCH NETBSDSRCDIR
		"$host_awk" -f "$EMBER_OS_SOURCE/etc/MAKEDEV.awk" \
			"$EMBER_OS_SOURCE/etc/MAKEDEV.tmpl" > "$actual_dest/dev/MAKEDEV"
		chmod 755 "$actual_dest/dev/MAKEDEV"
	fi
	actual_root=$work/actual-root
	mkdir -p "$actual_root/etc" "$actual_root/dev" "$actual_root/tests"
	mkdir -p "$actual_root/restricted dir"
	printf 'private fixture\n' > "$actual_root/restricted dir/private"
	chmod 000 "$actual_root/restricted dir/private"
	chmod 111 "$actual_root/restricted dir"
	cp "$here/target-acceptance-rc" "$actual_root/etc/rc"
	chmod 755 "$actual_root/etc/rc"
	printf 'same-build FFS fixture\n' > "$actual_root/etc/fixture"
	dd if=/dev/zero of="$actual_root/etc/.fixture-padding" bs=2048 count=512 2>/dev/null
	actual_image=$work/actual-root.ffs
	"$host_sh" "$helper" "$actual_root" "$actual_dest" "$EMBER_OS_TOOLDIR" \
	"$EMBER_OS_SOURCE" "$EMBER_ROOT_DEVICE" "$actual_image" "$host_sh"
	[ -s "$actual_image" ] || { echo 'same-build nbmakefs produced no FFS image' >&2; exit 1; }
	"$PYTHON" - "$actual_root" <<'PY'
import os, stat, sys
root = sys.argv[1]
assert stat.S_IMODE(os.stat(root + '/restricted dir').st_mode) == 0o111
assert stat.S_IMODE(os.stat(root + '/restricted dir/private').st_mode) == 0o000
PY
	chmod 700 "$actual_root/restricted dir"
	echo 'PASS: same-build nbmtree/nbmakefs created an FFS fixture'
fi
echo 'PASS: target root assembly validates device input and routes through OS tools'
