#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Exercise root assembly with fixture tools and optionally selected OS tools.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
helper=$here/target-acceptance-root.sh
work=$(mktemp -d "${TMPDIR:-/tmp}/target-acceptance-root.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/root/etc" "$work/root/dev" "$work/root/tests" \
	"$work/dest/etc" "$work/os/etc" "$work/tools/bin"
printf '#!/bin/sh\nexit 0\n' > "$work/root/etc/rc"
printf 'nonce-test\n' > "$work/root/etc/emberbsd-target-acceptance.nonce"
printf 'ld0a\n' > "$work/root/etc/emberbsd-target-acceptance-root-device"
printf '#!/bin/sh\nexit 0\n' > "$work/root/tests/run-target.sh"
cat > "$work/dest/etc/MAKEDEV" <<'EOF'
#!/bin/sh
printf '. type=dir\n./ld0a type=block mode=640\n./rld0a type=char mode=640\n'
EOF
cat > "$work/tools/bin/nbmtree" <<'EOF'
#!/bin/sh
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
cat > "$work/tools/bin/nbmakefs" <<'EOF'
#!/bin/sh
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
awk '
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
chmod 755 "$work/tools/bin/nbmtree" "$work/tools/bin/nbmakefs" "$work/dest/etc/MAKEDEV"

sh "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ld0a "$work/root.ffs" /bin/sh
[ "$(cat "$work/root.ffs")" = 'fixture only' ]
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

if sh "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ldXa "$work/invalid.ffs" /bin/sh >/dev/null 2>&1; then
	echo 'invalid root device unexpectedly passed' >&2
	exit 1
fi
[ ! -e "$work/invalid.ffs" ]
if OMIT_SET=yes sh "$helper" "$work/root" "$work/dest" "$work/tools" "$work/os" \
	ld0a "$work/no-mtree-default.ffs" /bin/sh >/dev/null 2>&1; then
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
		mkdir -p "$actual_dest/etc"
		MACHINE=aarch64
		MACHINE_ARCH=aarch64
		NETBSDSRCDIR=$EMBER_OS_SOURCE
		export MACHINE MACHINE_ARCH NETBSDSRCDIR
		awk -f "$EMBER_OS_SOURCE/etc/MAKEDEV.awk" \
			"$EMBER_OS_SOURCE/etc/MAKEDEV.tmpl" > "$actual_dest/etc/MAKEDEV"
		chmod 755 "$actual_dest/etc/MAKEDEV"
	fi
	actual_root=$work/actual-root
	mkdir -p "$actual_root/etc" "$actual_root/dev" "$actual_root/tests"
	printf 'same-build FFS fixture\n' > "$actual_root/etc/fixture"
	dd if=/dev/zero of="$actual_root/etc/.fixture-padding" bs=2048 count=512 2>/dev/null
	actual_image=$work/actual-root.ffs
	sh "$helper" "$actual_root" "$actual_dest" "$EMBER_OS_TOOLDIR" \
		"$EMBER_OS_SOURCE" "$EMBER_ROOT_DEVICE" "$actual_image" /bin/sh
	[ -s "$actual_image" ] || { echo 'same-build nbmakefs produced no FFS image' >&2; exit 1; }
	echo 'PASS: same-build nbmtree/nbmakefs created an FFS fixture'
fi
echo 'PASS: target root assembly validates device input and routes through OS tools'
