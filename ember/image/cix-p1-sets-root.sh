#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Assemble a CIX P1 root filesystem from the
# base and etc release sets and a package bundle. No block device is written.
set -eu
export LC_ALL=C
[ "$#" -eq 5 ] || {
	echo "usage: $0 SETS PACKAGES PORTS_REV TOOLDIR NEW_OUTPUT" >&2
	exit 2
}
sets=$1 packages=$2 ports_rev=$3 tools=$4 out=$5
: "${EMBER_HOST_PREFIX:?Set EMBER_HOST_PREFIX to the selected native SDK prefix}"
python=$EMBER_HOST_PREFIX/bin/python3.15
label=${CIX_ROOT_LABEL:-ember-cix-sets}
size=${CIX_ROOT_SIZE:-4g}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
profile_src=$(CDPATH= cd -- "$here/../.." && pwd)
for path in "$sets" "$packages" "$tools" "$out"; do
	case "$path" in /*) ;; *) echo 'Absolute paths required.' >&2; exit 2 ;; esac
	case "$path" in *[!A-Za-z0-9_./-]*) echo 'Unsafe path.' >&2; exit 2 ;; esac
done
case "$label" in ''|-*|*[!a-z0-9-]*) echo 'Unsafe root label.' >&2; exit 2 ;; esac
case "$size" in [1-9]*[mg]) ;; *) echo 'Size must be like 4g or 3072m.' >&2; exit 2 ;; esac
case "${size%[mg]}" in *[!0-9]*) echo 'Size must be like 4g or 3072m.' >&2; exit 2 ;; esac
[ "${#ports_rev}" -eq 40 ] || { echo 'PORTS_REV must be a full commit ID.' >&2; exit 2; }
case "$ports_rev" in *[!0-9a-f]*) echo 'PORTS_REV must be a full commit ID.' >&2; exit 2 ;; esac
[ ! -e "$out" ] && [ ! -L "$out" ] || { echo 'NEW_OUTPUT exists.' >&2; exit 2; }
[ -x "$tools/bin/nbmakefs" ] || { echo 'TOOLDIR has no nbmakefs.' >&2; exit 2; }
[ -x "$python" ] || { echo 'The native prefix has no python3.15.' >&2; exit 2; }
profile_revision=$(git -C "$profile_src" rev-parse HEAD)
git -C "$profile_src" diff --quiet
git -C "$profile_src" diff --cached --quiet

# The sets must match both the release checksum list and the build profile.
for set in base etc; do
	[ -f "$sets/$set.tar.xz" ] && [ ! -L "$sets/$set.tar.xz" ] || {
		echo "Missing set: $set.tar.xz" >&2; exit 2
	}
done
[ -f "$sets/SHA256" ] && [ -f "$sets/build-profile.json" ] || {
	echo 'SETS needs SHA256 and build-profile.json.' >&2; exit 2
}
release=$("$python" -B -c '
import json, sys
p = json.load(open(sys.argv[1]))
out = p.get("output_sha256", {})
sums = {k.rsplit("/", 1)[-1]: v for k, v in out.items() if k.endswith(".tar.xz")}
if p.get("status") != "built" or p.get("architecture") != "aarch64":
    sys.exit("The build profile is not a built aarch64 release.")
print(p["source_commit"], sums.get("base.tar.xz", "-"), sums.get("etc.tar.xz", "-"))
' "$sets/build-profile.json")
set -- $release
os_rev=$1 profile_base=$2 profile_etc=$3
[ "${#os_rev}" -eq 40 ] || { echo 'Bad source commit in the build profile.' >&2; exit 2; }
case "$os_rev" in *[!0-9a-f]*) echo 'Bad source commit in the build profile.' >&2; exit 2 ;; esac
for set in base etc; do
	actual=$(shasum -a 256 "$sets/$set.tar.xz" | awk '{print $1}')
	listed=$(awk -v n="$set.tar.xz" 'NF == 2 && $2 == n {print $1}' "$sets/SHA256")
	eval "from_profile=\$profile_$set"
	[ "$actual" = "$listed" ] && [ "$actual" = "$from_profile" ] || {
		echo "Checksum mismatch: $set.tar.xz" >&2; exit 2
	}
done

work=$(mktemp -d "${TMPDIR:-/tmp}/cix-sets-root.XXXXXX")
trap 'rm -rf "$work"' 0
trap 'exit 1' 1 2 3 15

# A case-insensitive build volume merges paths that differ only in case.
for set in base etc; do tar -tf "$sets/$set.tar.xz"; done | sed 's,/$,,' |
    sort -u > "$work/set-paths"
tr 'A-Z' 'a-z' < "$work/set-paths" | sort | uniq -d > "$work/case-collisions"
[ ! -s "$work/case-collisions" ] || {
	echo 'Set paths collide on a case-insensitive volume:' >&2
	cat "$work/case-collisions" >&2
	exit 2
}

# Package bundle: regular archives for this target. pkg_add on the target
# resolves the dependencies from this bundle only.
: > "$work/packages.sha256"
: > "$work/rcd.list"
mkdir "$work/rcd" "$work/meta"
count=0
for archive in "$packages"/*.tgz; do
	[ -e "$archive" ] || continue
	[ -f "$archive" ] && [ ! -L "$archive" ] || {
		echo "Package bundle entry is not a regular file: $archive" >&2; exit 2
	}
	name=${archive##*/}
	case "$name" in [A-Za-z0-9]*.tgz) ;; *) echo "Unsafe package name: $name" >&2; exit 2 ;; esac
	case "${name%.tgz}" in *[!A-Za-z0-9_.+-]*) echo "Unsafe package name: $name" >&2; exit 2 ;; esac
	meta=$work/meta/${name%.tgz}
	mkdir "$meta"
	tar -xzf "$archive" -C "$meta" --include '+*'
	[ -f "$meta/+CONTENTS" ] && [ -f "$meta/+BUILD_INFO" ] || {
		echo "Package has no metadata: $name" >&2; exit 2
	}
	[ "$(sed -n 's/^@name //p' "$meta/+CONTENTS")" = "${name%.tgz}" ] || {
		echo "Package name differs from its archive: $name" >&2; exit 2
	}
	grep -qx 'MACHINE_ARCH=aarch64' "$meta/+BUILD_INFO" &&
	    grep -qx 'OPSYS=NetBSD' "$meta/+BUILD_INFO" || {
		echo "Package is not for NetBSD/aarch64: $name" >&2; exit 2
	}
	[ "$(sed -n 's/^@cwd //p' "$meta/+CONTENTS" | head -n 1)" = /usr/pkg ] || {
		echo "Package prefix is not /usr/pkg: $name" >&2; exit 2
	}
	# rcorder(8) reads /etc/rc.d once, before the first boot installs the
	# packages. Place each declared rc.d script now; the installer compares
	# it with the installed example.
	if [ -f "$meta/+INSTALL" ]; then
		awk '$1 == "#" && $2 == "FILE:" && $3 ~ /^\/etc\/rc\.d\// && $4 ~ /r/ {
		    print $3, $5, $6 }' "$meta/+INSTALL" > "$meta/rcd"
		while read -r target example mode; do
			rcd=${target#/etc/rc.d/}
			case "$rcd" in ''|*[!a-z0-9_]*) echo "Unsafe rc.d name: $target" >&2; exit 2 ;; esac
			[ "$example" = "share/examples/rc.d/$rcd" ] || {
				echo "Unexpected rc.d example: $example" >&2; exit 2
			}
			case "$mode" in 0[0-7][0-7][0-7]) ;; *) echo "Bad rc.d mode: $mode" >&2; exit 2 ;; esac
			if [ -e "$work/rcd/$rcd" ] || grep -qx "./etc/rc.d/$rcd" "$work/set-paths"; then
				echo "Duplicate rc.d script: $rcd" >&2; exit 2
			fi
			tar -xzf "$archive" -O "$example" > "$work/rcd/$rcd"
			[ -s "$work/rcd/$rcd" ] || { echo "Empty rc.d script: $rcd" >&2; exit 2; }
			printf '%s %s\n' "$rcd" "$mode" >> "$work/rcd.list"
		done < "$meta/rcd"
	fi
	printf '%s  %s\n' "$(shasum -a 256 "$archive" | awk '{print $1}')" "$name" \
	    >> "$work/packages.sha256"
	count=$((count + 1))
done
[ "$count" -gt 0 ] || { echo 'The package bundle is empty.' >&2; exit 2; }
# The target installs from this bundle only: each dependency must match one
# of its packages, or the first boot fails on the board.
pkg_admin=$EMBER_HOST_PREFIX/sbin/pkg_admin
[ -x "$pkg_admin" ] || { echo 'The native prefix has no pkg_admin.' >&2; exit 2; }
ls "$work/meta" > "$work/names"
for meta in "$work/meta"/*; do
	sed -n 's/^@pkgdep //p' "$meta/+CONTENTS" > "$meta/deps"
	while read -r pattern; do
		found=
		while read -r candidate; do
			if "$pkg_admin" pmatch "$pattern" "$candidate"; then found=1; break; fi
		done < "$work/names"
		[ -n "$found" ] || {
			echo "No bundle package matches $pattern (${meta##*/})." >&2; exit 2
		}
	done < "$meta/deps"
done

mkdir -p "$out/root"
root=$out/root
for set in base etc; do
	tar -xpf "$sets/$set.tar.xz" -C "$root" --no-same-owner
done
for file in etc/mtree/NetBSD.dist etc/mtree/set.base etc/mtree/set.etc dev/MAKEDEV; do
	[ -s "$root/$file" ] || { echo "Missing set file: $file" >&2; exit 2; }
done

# Board layer.
cat > "$root/etc/fstab" <<EOF
NAME=$label / ffs rw,log,noatime 1 1
ptyfs /dev/pts ptyfs rw
tmpfs /tmp tmpfs rw,-m1777,-sram%25
EOF
cat >> "$root/etc/rc.conf" <<'EOF'

# CIX P1 root from release sets (ember/image/cix-p1-sets-root.sh).
rc_configured=YES
hostname=ember-cix
no_swap=YES
savecore=NO
postfix=NO
powerd=YES
sshd=YES
ember_packages=YES
# Wi-Fi starts when /etc/rc.conf.d enables these services next to
# a provisioned /etc/wpa_supplicant.conf.
wpa_supplicant=NO
wpa_supplicant_flags="-s -Dbsd -i rtwce0 -c /etc/wpa_supplicant.conf"
dhcpcd=NO
dhcpcd_flags="-b -C wpa_supplicant rtwce0"
# Package services start when /etc/rc.conf.d enables them.
EOF
while read -r rcd mode; do
	printf '%s=NO\n' "$rcd"
done < "$work/rcd.list" >> "$root/etc/rc.conf"
# Automatic roaming can select a BSS before wpa_supplicant starts.
printf '%s\n' down 'roaming manual' up > "$root/etc/ifconfig.rtwce0"
mkdir -p "$root/etc/rc.conf.d" "$root/var/ember/packages" "$root/usr/libexec/ember"
bundle=$root/var/ember/packages
while read -r hash name; do
	cp "$packages/$name" "$bundle/$name"
done < "$work/packages.sha256"
cp "$work/packages.sha256" "$bundle/packages.sha256"
printf '%s\n' "$ports_rev" > "$bundle/ports-revision"
awk '{print $1}' "$work/rcd.list" > "$bundle/rc.d.list"
while read -r rcd mode; do
	cp "$work/rcd/$rcd" "$root/etc/rc.d/$rcd"
done < "$work/rcd.list"
cp "$here/install-packages.sh" "$root/usr/libexec/ember/install-packages"
cp "$here/../etc/rc.d/ember_packages" "$root/etc/rc.d/ember_packages"
sh "$here/install-packages.sh" --verify "$bundle"
{
	echo 'EmberBSD CIX P1 root from release sets'
	printf 'os=%s\nports=%s\nprofile=%s\nlabel=%s\n' \
	    "$os_rev" "$ports_rev" "$profile_revision" "$label"
} > "$root/etc/ember-build"

# Target ownership and modes come from the distribution and the explicit
# entries below, never from the extracting host. The set entries also carry
# size= and makefs uses it: an edited set file needs its own entry.
awk '$1 == "./etc/fstab" || $1 == "./etc/rc.conf" { next } { print }' \
    "$root/etc/mtree/NetBSD.dist" "$root/etc/mtree/set.base" \
    "$root/etc/mtree/set.etc" > "$work/spec"
# Keep a MAKEDEV failure visible instead of hiding it in a pipeline.
(cd "$root/dev" && /bin/sh ./MAKEDEV -s all) > "$work/devices.spec"
sed -e '/^\. type=dir/d' -e 's:^\./:./dev/:' "$work/devices.spec" >> "$work/spec"
{
	for file in etc/fstab etc/rc.conf etc/ifconfig.rtwce0 etc/ember-build; do
		printf './%s type=file uname=root gname=wheel mode=0644\n' "$file"
	done
	echo './etc/rc.conf.d type=dir uname=root gname=wheel mode=0755'
	echo './etc/rc.d/ember_packages type=file uname=root gname=wheel mode=0555'
	echo './usr/libexec/ember type=dir uname=root gname=wheel mode=0755'
	echo './usr/libexec/ember/install-packages type=file uname=root gname=wheel mode=0555'
	echo './var/ember type=dir uname=root gname=wheel mode=0755'
	echo './var/ember/packages type=dir uname=root gname=wheel mode=0755'
	for file in packages.sha256 ports-revision rc.d.list; do
		printf './var/ember/packages/%s type=file uname=root gname=wheel mode=0444\n' "$file"
	done
	while read -r hash name; do
		printf './var/ember/packages/%s type=file uname=root gname=wheel mode=0444\n' "$name"
	done < "$work/packages.sha256"
	while read -r rcd mode; do
		printf './etc/rc.d/%s type=file uname=root gname=wheel mode=%s\n' "$rcd" "$mode"
	done < "$work/rcd.list"
} >> "$work/spec"
# makefs and the check below need to traverse directories whose target
# mode is restrictive.
find "$root" -type d -exec chmod u+rwx {} \;
# makefs -x drops files without a spec entry; refuse such a silent loss.
"$python" -B -c '
import os, re, sys
spec, root = sys.argv[1].encode(), sys.argv[2].encode()
def decode(word):
    if re.search(rb"\\(?![0-7]{3}|\\)", word):
        sys.exit("Unsupported mtree escape: %r" % word)
    return re.sub(rb"\\([0-7]{3}|\\)", lambda m: b"\\" if m.group(1) == b"\\"
                  else bytes([int(m.group(1), 8)]), word)
listed = {decode(line.split()[0]) for line in open(spec, "rb") if line.startswith(b"./")}
missing = []
def fail(error):
    raise error
for top, dirs, files in os.walk(root, onerror=fail):
    for name in dirs + files:
        path = b"./" + os.path.relpath(os.path.join(top, name), root)
        if path not in listed and not path.startswith(b"./dev/"):
            missing.append(path.decode(errors="backslashreplace"))
if missing:
    sys.exit("Root files without target metadata:\n" + "\n".join(sorted(missing)))
' "$work/spec" "$root"
cp "$work/spec" "$out/root.spec"
"$tools/bin/nbmakefs" -Zx -t ffs -B le -s "$size" -N "$root/etc" \
    -o version=2,bsize=16384,fsize=2048,density=8192 \
    -F "$out/root.spec" "$out/root.fs" "$root"
git -C "$profile_src" diff --quiet
git -C "$profile_src" diff --cached --quiet
[ "$(git -C "$profile_src" rev-parse HEAD)" = "$profile_revision" ]
{
	printf 'os=%s\nports=%s\nprofile=%s\nlabel=%s\nsize=%s\n' \
	    "$os_rev" "$ports_rev" "$profile_revision" "$label" "$size"
	for set in base etc; do
		printf 'SHA256 (%s.tar.xz) = %s\n' "$set" \
		    "$(shasum -a 256 "$sets/$set.tar.xz" | awk '{print $1}')"
	done
	printf 'SHA256 (packages.sha256) = %s\n' \
	    "$(shasum -a 256 "$bundle/packages.sha256" | awk '{print $1}')"
} > "$out/ROOT-INPUTS.txt"
(cd "$out" && shasum -a 256 root.fs) > "$out/ROOT-SHA256.txt"
echo "Created $out/root.fs ($size, wedge name $label). Provision it before the first boot."
