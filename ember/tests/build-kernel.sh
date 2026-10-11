#!/bin/sh
# Origin: EmberBSD (AI-assisted), kernel build orchestration regression.
# Exercise build orchestration without substituting these fixtures for a build.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-build-kernel.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
export TEST_ROOT="$work" TEST_LOG="$work/commands"
mkdir -p "$work/src/ember/tools" "$work/bin" "$work/src/tools" \
    "$work/src/share/mk" "$work/src/sys/arch/evbarm/conf" \
    "$work/src/sys/external/gpl2/dts/dist"
cp "$repo/ember/build-kernel.sh" "$work/src/ember/"
cp "$repo/ember/tools/kernel-contracts.sh" "$work/src/ember/tools/"
for contract in bcm2712-v3d-contract bcm2712-v3d-takeover-contract \
    bcm2712-v3d-dma-contract bcm2712-v3d-queue-contract \
    bcm2712-v3d-irq-contract bcm2712-v3d-fault-contract; do
    cat > "$work/src/ember/tools/$contract.sh" <<'SH'
#!/bin/sh
echo "contract $0 $*" >> "$TEST_LOG"
[ "${FAIL_AT:-}" != contract ]
SH
done
: > "$work/src/tools/Makefile"
: > "$work/src/share/mk/bsd.own.mk"
: > "$work/src/sys/arch/evbarm/conf/EMBER64"
for module in if_cemac_acpi bcm2712btcom rp1wmcodec rpi5button; do
    mkdir -p "$work/src/sys/modules/$module"
done
cat > "$work/bin/uname" <<'SH'
#!/bin/sh
case $1 in -s) echo "${TEST_HOST:-Darwin}";; -m|-p) echo aarch64;; -r) echo 11.0;; esac
SH
cat > "$work/bin/python3" <<'SH'
#!/bin/sh
echo "contract $*" >> "$TEST_LOG"
if [ "${TEST_HOST:-Darwin}" != NetBSD ]; then
    case $1 in */btuart-contract.py|*/codec-contract.py|*/bluetooth-control-contract.py|*/bluetooth-pair-contract.py) exit 1;; esac
fi

[ "${FAIL_AT:-}" != contract ]
SH
cat > "$work/src/build.sh" <<'SH'
#!/bin/sh
set -eu
echo "build.sh $*" >> "$TEST_LOG"
[ "${MAKECONF:-}" = /dev/null ]
[ "${FAIL_AT:-}" != tools ]
mkdir -p "$TEST_OUT/tools/bin" "$TEST_OUT/obj/kernels/EMBER64"
cp "$TEST_ROOT/bin/nbmake-evbarm" "$TEST_OUT/tools/bin/"
cp "$TEST_ROOT/bin/cpp" "$TEST_OUT/tools/bin/target-cpp"
cp "$TEST_ROOT/bin/dtc" "$TEST_OUT/tools/bin/nbdtc"
printf kernel > "$TEST_OUT/obj/kernels/EMBER64/netbsd"
printf image > "$TEST_OUT/obj/kernels/EMBER64/netbsd.img"
SH
cat > "$work/bin/nbmake-evbarm" <<'SH'
#!/bin/sh
set -eu
echo "nbmake $*" >> "$TEST_LOG"
dir=$PWD
while [ $# -gt 0 ]; do
    case $1 in
    -C) dir=$2; shift;;
    -V) case $2 in
        CPP) echo '${TOOL_CPP.${ACTIVE_CPP}}';;
        '${CPP}') echo "$TEST_OUT/tools/bin/target-cpp";;
        TOOL_DTC) echo '${TOOLDIR}/bin/${_TOOL_PREFIX}dtc';;
        '${TOOL_DTC}') echo "$TEST_OUT/tools/bin/nbdtc";;
        .OBJDIR) echo "$TEST_OUT/obj/modules/${dir##*/}";;
        esac
        exit;;
    esac
    shift
done
case "$dir" in */sys/modules/*)
    [ "${FAIL_AT:-}" != module ]
    mkdir -p "$TEST_OUT/obj/modules/${dir##*/}"
    printf module > "$TEST_OUT/obj/modules/${dir##*/}/${dir##*/}.kmod";;
esac
SH
cat > "$work/bin/config" <<'SH'
#!/bin/sh
mkdir -p "../compile/$1"
SH
cat > "$work/bin/make" <<'SH'
#!/bin/sh
set -eu
echo "native-make $*" >> "$TEST_LOG"
if [ "${1:-}" = -V ]; then echo "$PWD"; exit; fi
[ "${FAIL_AT:-}" != kernel ]
case $PWD in
*/compile/*) printf kernel > netbsd; printf image > netbsd.img;;
*/modules/*) printf module > "${PWD##*/}.kmod";;
esac
SH
cat > "$work/bin/cpp" <<'SH'
#!/bin/sh
echo "cpp $0 $*" >> "$TEST_LOG"
[ "${FAIL_AT:-}" != cpp ] || exit 1
echo '/* fixture */'
SH
cat > "$work/bin/dtc" <<'SH'
#!/bin/sh
set -eu
echo "dtc $0 $*" >> "$TEST_LOG"
[ "${FAIL_AT:-}" != dtb ]
while [ $# -gt 0 ]; do
    if [ "$1" = -o ]; then printf dtb > "$2"; exit; fi
    shift
done
exit 1
SH
cat > "$work/bin/cp" <<'SCRIPT'
#!/bin/sh
/bin/cp "$@" || exit
for last do :; done
if [ "$last" = "$(cd "$TEST_OUT" && pwd -P)/" ]; then
    [ "${FAIL_AT:-}" != publish ] || exit 1
fi
SCRIPT
chmod +x "$work/bin/"* "$work/src/build.sh"
export PATH="$work/bin:$PATH" NETBSD2_PYTHON="$work/bin/python3" NETBSD2_JOBS=2
export TEST_OUT="$work/cross"
sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1 || {
    cat "$work/run.log"; exit 1;
}
test -s "$TEST_OUT/netbsd-EMBER64"
test -s "$TEST_OUT/rpi5button.kmod"
test -s "$TEST_OUT/sun60i-a733-orangepi-zero3w.dtb"
grep -q 'build.sh .* -m evbarm -a aarch64 ' "$TEST_LOG"
grep -q ' -u ' "$TEST_LOG"
grep -q 'EMBER_KERNEL_OBJDIR=.*/obj/kernels/EMBER64' "$TEST_LOG"
grep -q 'cpp .*/tools/bin/target-cpp ' "$TEST_LOG"
grep -q 'dtc .*/tools/bin/nbdtc ' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
test "$(grep -c '^contract ' "$TEST_LOG")" -eq 8
for failure in tools module cpp dtb contract publish; do
    printf stale > "$TEST_OUT/netbsd-EMBER64"
    if FAIL_AT=$failure sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1; then
        echo "accepted failure: $failure" >&2; exit 1
    fi
    test ! -e "$TEST_OUT/netbsd-EMBER64"
done
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_EXTERNAL_TOOLCHAIN=/cross/gcc16 \
    sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q 'EXTERNAL_TOOLCHAIN=/cross/gcc16' "$TEST_LOG"
grep -q 'TOOLCHAIN_MISSING=yes' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
export TEST_OUT="$work/native"
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_BUILD_MODE=native sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q native-make "$TEST_LOG"
! grep -q build.sh "$TEST_LOG"
test -s "$TEST_OUT/bcm2712btcom.kmod"
test "$(grep -c '^contract ' "$TEST_LOG")" -eq 12
if EMBER_BUILD_MODE=native sh "$work/src/ember/build-kernel.sh" "$work/wrong-host" > "$work/run.log" 2>&1; then
    echo 'accepted native tools on Darwin' >&2; exit 1
fi
if NETBSD2_JOBS=00 sh "$work/src/ember/build-kernel.sh" "$work/zero-jobs" > "$work/run.log" 2>&1; then
    echo 'accepted zero jobs' >&2; exit 1
fi
grep -q 'positive integer' "$work/run.log"
rm "$work/src/tools/Makefile"
if sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1; then
    echo 'accepted incomplete cross source' >&2; exit 1
fi
grep -q 'full source' "$work/run.log"
test ! -e "$TEST_OUT/netbsd-EMBER64"
test ! -e "$TEST_OUT/bcm2712btcom.kmod"
echo 'PASS: cross/native routing, matched module headers, external tools, eight portable/twelve native contracts, failed-build outputs'
