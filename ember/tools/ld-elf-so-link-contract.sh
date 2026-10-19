#!/bin/sh
# Origin: EmberBSD - relative loader links for a DESTDIR or a cross sysroot.
# SPDX-License-Identifier: BSD-2-Clause
# Each link made when SHLINKINSTALLDIR is not /usr/libexec must stay in the tree.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
mk="$src/libexec/ld.elf_so/Makefile"
fail() { echo "FAIL: $*" >&2; exit 1; }
# Print each "target link" pair of the conditional block, joined across lines.
pairs=$(awk '
	/^\.if \$\{SHLINKINSTALLDIR\} != "\/usr\/libexec"$/ { block = 1; next }
	block && /^\.endif$/ && !nested { exit }
	block && /^\.if / { nested++; next }
	block && /^\.endif$/ { nested--; next }
	block && /^SYMLINKS\+=/ { sub(/^SYMLINKS\+=[[:space:]]*/, ""); entry = 1 }
	block && entry {
		line = $0; more = sub(/[[:space:]]*\\$/, "", line)
		gsub(/^[[:space:]]+/, "", line); text = text " " line
		if (!more) { print text; text = ""; entry = 0 }
	}
' "$mk")
[ "$(printf '%s\n' "$pairs" | grep -c .)" -eq 2 ] || fail "expected two link entries"
resolve() {
	printf '%s/%s\n' "$1" "$2" | awk -F/ '{
		n = 0
		for (i = 2; i <= NF; i++) {
			if ($i == "..") { if (n > 0) n-- }
			else if ($i != "" && $i != ".") p[++n] = $i
		}
		out = ""
		for (i = 1; i <= n; i++) out = out "/" p[i]
		print out
	}'
}
checks=0
for prog in ld.elf_so ld.elf_so-i386; do
	printf '%s\n' "$pairs" | while read -r target link; do
		expand() {
			printf '%s\n' "$1" | sed -e 's|\${SHLINKINSTALLDIR}|/libexec|g' \
			    -e 's|\${BINDIR}|/libexec|g' -e 's|\${DEBUGDIR}|/usr/libdata/debug|g' \
			    -e "s|\${PROG}|$prog|g"
		}
		t=$(expand "$target"); l=$(expand "$link")
		case "$t" in /*) fail "absolute link target: $t";; esac
		case "$l" in
		/usr/libexec/$prog) want=/libexec/$prog;;
		/usr/libdata/debug/usr/libexec/$prog.debug)
			want=/usr/libdata/debug/libexec/$prog.debug;;
		*) fail "unexpected link path: $l";;
		esac
		got=$(resolve "${l%/*}" "$t")
		[ "$got" = "$want" ] || fail "$l -> $t resolves to $got, not $want"
	done
	checks=$((checks + 2))
done
echo "ld.elf_so link contract: $checks checks passed"
