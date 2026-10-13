#!/bin/sh
# Origin: EmberBSD; AI-assisted A733 preboot builder and failure regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/a733-preboot-test.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
builder=$tools/a733-preboot-snapshot.sh
normal=$src/ember/boot/orangepi-zero4-boot.cmd
checks=0
fail()
{
	echo "FAIL: $*" >&2
	exit 1
}
check()
{
	checks=$((checks + 1))
	"$@" || fail "$*"
}

sh "$builder" "$normal" "$work/boot.cmd"
sed '/^# BEGIN A733 PREBOOT SNAPSHOT/,/^# END A733 PREBOOT SNAPSHOT/d' \
    "$work/boot.cmd" > "$work/restored"
check cmp "$normal" "$work/restored"
awk '/^# BEGIN A733 PREBOOT SNAPSHOT/ { copying = 1 }
    copying { print }
    /^# END A733 PREBOOT SNAPSHOT/ { copying = 0 }' \
    "$work/boot.cmd" > "$work/snippet"
check sh -n "$work/boot.cmd"
check awk 'length > 1023 { exit 1 }' "$work/snippet"
check awk '/^# END A733 PREBOOT SNAPSHOT/ { getline;
    if ($0 != "booti 0x44000000 - ${fdt_addr_r}") exit 1; found = 1 }
    END { if (!found) exit 1 }' "$work/boot.cmd"

# Audit the added command vocabulary before executing it with host stubs.
# No board command, MMIO write, shell expansion, or fallback value is allowed.
sed '/^[ \t]*#/d; s/&&/\
/g; s/;/\
/g' "$work/snippet" | awk '
    /^[ \t]*#/ { next }
    { sub(/^[ \t]+/, ""); sub(/[ \t]+$/, ""); sub(/^if /, "") }
    /^$/ || /^(then|else|fi)$/ { next }
    /^setexpr\.l aps_[a-z0-9]+ \*0x[0-9a-f]+ \+ 0$/ { next }
    /^setexpr\.l aps_enabled \$\{aps_gate\} \\& 1$/ { next }
    /^setenv aps_[a-z0-9]+ unavailable$/ { next }
    /^test "\$\{aps_[a-z0-9]+\}" != unavailable$/ { next }
    /^test -n "\$\{aps_[a-z0-9]+\}"$/ { next }
    /^test "\$\{aps_enabled\}" = "1"$/ { next }
    /^fdt set \/chosen ember,a733-preboot-[a-z]+ / {
        if ($0 ~ /[`;]|\$\(/) exit 1
        next
    }
    { print "unexpected command: " $0; exit 1 }
' || fail "unexpected snapshot command"
sed 's/setexpr\.l /setexpr_l /g' "$work/snippet" > "$work/run"

cat > "$work/expected-addresses" <<'ADDRESSES'
0x070101ac
0x07065000
0x07065004
0x07065008
0x07065010
0x07065014
0x07065020
0x07065030
0x07065038
0x07066000
0x07066004
0x07066008
0x07066010
0x07066014
0x07066020
0x07066030
0x07066038
ADDRESSES

# Execute the generated control flow. These stubs model only the audited
# command subset; the pinned U-Boot parser is checked separately in the guide.
setenv()
{
	check test "$#" -eq 2
	check test "$2" = unavailable
	case "$1" in aps_gate|aps_enabled|aps_t[0-7]|aps_c[0-7]) ;; *) fail "variable $1" ;; esac
	inits=$((inits + 1))
	if [ "$inits" -eq "$fail_init" ]; then return 1; fi
	eval "$1=unavailable"
}
setexpr_l()
{
	check test "$#" -eq 4
	expressions=$((expressions + 1))
	if [ "$1" = aps_enabled ]; then
		check test "$3" = '&'
		check test "$4" = 1
		if [ "$expressions" -eq "$silent_expr" ]; then
			if [ "$silent_mode" = unset ]; then unset "$1"; fi
			return 0
		fi
		aps_enabled=$((0x$2 & 1))
		return 0
	fi
	check test "$3" = +
	check test "$4" = 0
	case "$2" in \*0x*) ;; *) fail "non-MMIO source: $2" ;; esac
	reads=$((reads + 1))
	address=${2#\*}
	printf '%s\n' "$address" >> "$work/reads"
	if [ "$reads" -eq "$fail_read" ]; then return 1; fi
	# Pinned setexpr returns success even when env_set_hex rejects a store.
	if [ "$expressions" -eq "$silent_expr" ]; then
		if [ "$silent_mode" = unset ]; then unset "$1"; fi
		return 0
	fi
	case "$address" in
	0x070101ac) value=$gate ;;
	0x07065000|0x07066000) value=8 ;;
	0x07065004) value=ffffffff ;;
	0x07065008) value=8 ;;
	0x07065014) value=100 ;;
	0x07065020|0x07066020) value=101 ;;
	0x07065010|0x07066010) value=1 ;;
	*) value=0 ;;
	esac
	case "$1" in aps_gate|aps_t[0-7]|aps_c[0-7]) ;; *) fail "variable $1" ;; esac
	eval "$1=\$value"
}
fdt()
{
	check test "$#" -eq 4
	check test "$1" = set
	check test "$2" = /chosen
	writes=$((writes + 1))
	if [ "$writes" -eq "$fail_fdt" ]; then return 1; fi
	case "$3" in
	ember,a733-preboot-status) status=$4 ;;
	ember,a733-preboot-version) check test "$4" = '<0x1>' ;;
	ember,a733-preboot-gate) check test "$4" = "<0x070101ac 0x${gate}>" ;;
	ember,a733-preboot-top)
		check test "$4" = '<0x07065000 0x8 0xffffffff 0x8 0x1 0x100 0x101 0x0 0x0>' ;;
	ember,a733-preboot-core)
		check test "$4" = '<0x07066000 0x8 0x0 0x0 0x1 0x0 0x101 0x0 0x0>' ;;
	*) fail "unexpected FDT property $3" ;;
	esac
}
run_snapshot()
{
	reads=0 writes=0 inits=0 expressions=0 status=missing
	# Stale values must not become a successful snapshot after a failed read.
	aps_gate=1 aps_enabled=1
	aps_t0=8 aps_t1=ffffffff aps_t2=8 aps_t3=1
	aps_t4=100 aps_t5=101 aps_t6=0 aps_t7=0
	aps_c0=8 aps_c1=0 aps_c2=0 aps_c3=1
	aps_c4=0 aps_c5=101 aps_c6=0 aps_c7=0
	: > "$work/reads"
	# U-Boot expands an absent environment variable to the empty string.
	set +eu
	. "$work/run"
	set -eu
	# The original booti follows this block regardless of diagnostic failure.
	boot_reached=yes
}
gate=1 fail_read=0 fail_fdt=0 fail_init=0 silent_expr=0 silent_mode=keep
run_snapshot
check test "$status" = complete
check test "$boot_reached" = yes
check test "$writes" -eq 6
check test "$inits" -eq 18
check test "$expressions" -eq 18
check cmp "$work/expected-addresses" "$work/reads"

for gate in 0 10000; do
	run_snapshot
	check test "$status" = ppu-clock-gated
	check test "$reads" -eq 1
	check test "$boot_reached" = yes
done
# Bit 16 is not a PPU reset. Only clock gate bit 0 authorizes PCK reads.
gate=10001
run_snapshot
check test "$status" = complete
check cmp "$work/expected-addresses" "$work/reads"
gate=1
fail_read=1
while [ "$fail_read" -le 17 ]; do
	run_snapshot
	check test "$status" != complete
	check test "$reads" -eq "$fail_read"
	check test "$boot_reached" = yes
	fail_read=$((fail_read + 1))
done
fail_read=0 fail_fdt=1
while [ "$fail_fdt" -le 6 ]; do
	run_snapshot
	check test "$status" != complete
	check test "$boot_reached" = yes
	fail_fdt=$((fail_fdt + 1))
done
fail_fdt=0
for silent_mode in keep unset; do
	silent_expr=1
	while [ "$silent_expr" -le 18 ]; do
		run_snapshot
		check test "$status" = started
		check test "$expressions" -eq "$silent_expr"
		check test "$boot_reached" = yes
		silent_expr=$((silent_expr + 1))
	done
done
silent_expr=0 fail_init=1
while [ "$fail_init" -le 18 ]; do
	run_snapshot
	check test "$status" != complete
	check test "$inits" -eq "$fail_init"
	check test "$expressions" -eq "$((fail_init - 1))"
	check test "$boot_reached" = yes
	fail_init=$((fail_init + 1))
done

reject()
{
	if sh "$builder" "$1" "$2" > "$work/error" 2>&1; then
		fail "builder accepted invalid input/output: $1 $2"
	fi
	checks=$((checks + 1))
}
reject "$normal" "$work/boot.cmd"
check cmp "$normal" "$work/restored"
reject "$work/boot.cmd" "$work/duplicate"
check test ! -e "$work/duplicate"
cat "$normal" "$normal" > "$work/two"
reject "$work/two" "$work/two-output"
sed '/^booti /d' "$normal" > "$work/no-boot"
reject "$work/no-boot" "$work/no-boot-output"
sed 's/^booti 0x44000000/booti 0x45000000/' "$normal" > "$work/other-boot"
reject "$work/other-boot" "$work/other-boot-output"
sed '/^fdt resize /d' "$normal" > "$work/no-space"
reject "$work/no-space" "$work/no-space-output"
printf 'booti 0x44000000 - ${fdt_addr_r}\nfdt addr ${fdt_addr_r}\nfdt resize 8192\n' > "$work/wrong-order"
reject "$work/wrong-order" "$work/wrong-order-output"
ln -s "$work/absent" "$work/link"
reject "$normal" "$work/link"
check test ! -e "$work/absent"
printf 'A733 preboot snapshot: %s checks passed\n' "$checks"
