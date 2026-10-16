#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: AI-assisted EmberBSD AArch64 sysroot publication, 2026-10-16.
# Build complete base and comp sets from a fresh, pinned source export.
set -eu

fail() { echo "$*" >&2; exit 1; }
[ "$#" -eq 2 ] || fail 'Usage: sh ember/release/build-aarch64.sh FULL-COMMIT /absolute/unused-output'
revision=$1
out=$2
case "$revision" in *[!0-9a-f]*|'') fail 'Use a full hexadecimal source commit.' ;; esac
[ "${#revision}" -eq 40 ] || fail 'Use a full source commit.'
case "$out" in /*) ;; *) fail 'Use an absolute output path.' ;; esac
case "$out" in *[[:space:]]*) fail 'The build output path must not contain whitespace.' ;; esac
case "$(basename -- "$out")" in ''|.|..) fail 'Use an unused output directory.' ;; esac
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
case "$root" in *[[:space:]]*) fail 'The source path must not contain whitespace.' ;; esac
out_parent=$(CDPATH= cd -- "$(dirname -- "$out")" && pwd -P)
out=$out_parent/$(basename -- "$out")
case "$out/" in "$root/"*) fail 'The output must be outside the source repository.' ;; esac
actual=$(git -C "$root" rev-parse --verify "$revision^{commit}")
[ "$actual" = "$revision" ] || fail 'The source pin must name the commit object itself.'
for path in build.sh tools/Makefile share/mk/bsd.own.mk sys/arch/evbarm/conf/EMBER64; do
	git -C "$root" cat-file -e "$revision:$path" || fail "The source commit is incomplete: $path"
done
jobs=${EMBER_RELEASE_JOBS:-4}
case "$jobs" in *[!0-9]*|'') fail 'Use a positive worker count.' ;; esac
[ "$jobs" -gt 0 ] 2>/dev/null || fail 'Use a positive worker count.'
[ -z "${EMBER_EXTERNAL_TOOLCHAIN:-}" ] && [ -z "${EXTERNAL_TOOLCHAIN:-}" ] ||
	fail 'This profile requires its matching in-tree bootstrap toolchain.'
host_cc=$(command -v cc) || fail 'A host C compiler is required.'
host_cxx=$(command -v c++) || fail 'A host C++ compiler is required.'
timestamp=$(git -C "$root" show -s --format=%ct "$revision")

sha256() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | awk '{print $1}'
	else
		shasum -a 256 "$1" | awk '{print $1}'
	fi
}
json_string() {
	# The input can contain compiler banners with several lines.
	LC_ALL=C awk 'BEGIN { printf "\"" }
		{ if (NR > 1) printf "\\n";
		  gsub(/\\/, "\\\\"); gsub(/\"/, "\\\"");
		  gsub(/\t/, "\\t"); gsub(/\r/, "\\r"); printf "%s", $0 }
		END { printf "\"" }'
}
json_value() { printf '%s\n' "$1" | json_string; }
json_argv() {
	printf '['
	separator=
	for argument do
		printf '%s' "$separator"
		json_value "$argument"
		separator=,
	done
	printf ']'
}
write_profile() {
	state=$1
	shift
	{
		printf '{"schema":1,"status":'
		json_value "$state"
		printf ',"source_repository":"https://github.com/oxtech-ember/EmberBSD","source_commit":'
		json_value "$revision"
		printf ',"source_timestamp":%s,"source_archive":"source.tar.gz","source_sha256":' "$timestamp"
		json_value "$source_sha"
		printf ',"machine":"evbarm","architecture":"aarch64","release_machine_directory":"evbarm-aarch64"'
		printf ',"profile":"aarch64-base-comp-no-x11","kernel_config":"EMBER64"'
		printf ',"toolchain":"in-tree GCC bootstrap; native GCC commands excluded; external GCC16 is a consumer toolchain"'
		printf ',"build_environment":{"PATH":'; json_value "$PATH"
		printf ',"HOME":'; json_value "${HOME:-/}"
		printf ',"TMPDIR":'; json_value "${TMPDIR:-/tmp}"
		printf ',"LC_ALL":"C","MAKECONF":"/dev/null"}'
		printf ',"host":{"uname":'; json_string < "$out/host-uname.txt"
		printf ',"cc":'; json_value "$host_cc"
		printf ',"cc_version":'; json_string < "$out/host-cc-version.txt"
		printf ',"cxx":'; json_value "$host_cxx"
		printf ',"cxx_version":'; json_string < "$out/host-cxx-version.txt"
		printf '},"target_cc_version":'
		if [ -s "$out/target-cc-version.txt" ]; then
			json_string < "$out/target-cc-version.txt"
		else
			printf null
		fi
		printf ',"commands":['
		json_argv env -i "PATH=$PATH" "HOME=${HOME:-/}" "TMPDIR=${TMPDIR:-/tmp}" LC_ALL=C MAKECONF=/dev/null \
		    sh ./build.sh "$@" tools kernel=EMBER64
		printf ','
		json_argv env -i "PATH=$PATH" "HOME=${HOME:-/}" "TMPDIR=${TMPDIR:-/tmp}" LC_ALL=C MAKECONF=/dev/null \
		    sh ./build.sh "$@" distribution sets
		printf '],"output_sha256":{'
		separator=
		for path in netbsd-EMBER64.gz release/evbarm-aarch64/binary/sets/base.tar.xz release/evbarm-aarch64/binary/sets/comp.tar.xz; do
			if [ -s "$out/$path" ]; then
				printf '%s' "$separator"; json_value "$path"; printf ':'
				json_value "$(sha256 "$out/$path")"
				separator=,
			fi
		done
		printf '}}\n'
	} > "$out/build-profile.json.tmp"
	mv "$out/build-profile.json.tmp" "$out/build-profile.json"
}

# mkdir refuses existing directories and symbolic links. No old success can survive.
mkdir "$out"
git -C "$root" archive --format=tar.gz -o "$out/source.tar.gz" "$revision"
source_sha=$(sha256 "$out/source.tar.gz")
mkdir "$out/source"
tar -xzf "$out/source.tar.gz" -C "$out/source"
uname -a > "$out/host-uname.txt"
"$host_cc" --version > "$out/host-cc-version.txt" 2>&1
"$host_cxx" --version > "$out/host-cxx-version.txt" 2>&1
cd "$out/source"

# Keep C/C++ headers, runtime, link libraries, licences and firmware. The standard
# distribution and sets targets retain their normal METALOG and checkflist checks.
# No release/install-image target can select all board kernels or disk images.
set -- -U -u -m evbarm -a aarch64 -j"$jobs" \
	-O "$out/obj" -T "$out/tools" -D "$out/dest" -R "$out/release" \
	-V "HOST_CC=$host_cc" -V "HOST_CXX=$host_cxx" \
	-V USETOOLS=yes -V HAVE_GCC=12 -V TOOLCHAIN_MISSING=no \
	-V MKGCC=yes -V MKGCCCMDS=no -V MKCXX=yes -V MKLIBSTDCXX=yes \
	-V MKLINKLIB=yes -V MKSTATICLIB=yes -V MKPIC=yes -V MKPICLIB=yes \
	-V MKDYNAMICROOT=yes -V MKFIRMWARE=yes -V MKKMOD=no \
	-V MKX11=no -V MKRUMP=no -V MKCOMPAT=no -V MKLLVM=no -V MKLLVMRT=no \
	-V MKDEBUG=no -V MKDEBUGLIB=no -V MKDEBUGKERNEL=no \
	-V MKATF=no -V MKKYUA=no -V MKMAN=no -V MKDOC=no -V MKINFO=no \
	-V MKHTML=no -V MKLINT=no -V MKPROFILE=no -V MKCVS=no -V MKGROFF=no \
	-V MKGDB=no -V MKBINUTILS=no -V MKREPRO=yes -V "MKREPRO_TIMESTAMP=$timestamp" \
	-V "KERNOBJDIR=$out/obj/kernels" -V RELEASEMACHINEDIR=evbarm-aarch64 \
	-V USE_XZ_SETS=yes -V 'MAKETARSETS=base comp' \
	-V EMBER_VERSION=1.0 -V "EMBER_BUILD_ID=$revision" -V "BUILDINFO=EmberBSD:$revision"
write_profile started "$@"
printf 'Build log: %s/build.log\n' "$out"
{
	env -i "PATH=$PATH" "HOME=${HOME:-/}" "TMPDIR=${TMPDIR:-/tmp}" LC_ALL=C MAKECONF=/dev/null \
	    sh ./build.sh "$@" tools kernel=EMBER64
	"$out/tools/bin/aarch64--netbsd-gcc" --version > "$out/target-cc-version.txt" 2>&1
	env -i "PATH=$PATH" "HOME=${HOME:-/}" "TMPDIR=${TMPDIR:-/tmp}" LC_ALL=C MAKECONF=/dev/null \
	    sh ./build.sh "$@" distribution sets
} > "$out/build.log" 2>&1
for path in obj/kernels/EMBER64/netbsd release/evbarm-aarch64/binary/sets/base.tar.xz release/evbarm-aarch64/binary/sets/comp.tar.xz; do
	[ -s "$out/$path" ] || fail "The build did not create $path"
done
gzip -n -c "$out/obj/kernels/EMBER64/netbsd" > "$out/netbsd-EMBER64.gz"
write_profile built "$@"
printf '%s\n' "$revision" > "$out/build-success.tmp"
mv "$out/build-success.tmp" "$out/build-success"
printf 'Completed pinned AArch64 base/comp build: %s\n' "$revision"
