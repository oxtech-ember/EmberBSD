#!/bin/sh
# Build the AArch64 kernel, matched modules and board device trees.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
fail() { echo "$*" >&2; exit 1; }
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || fail 'usage: build-kernel.sh ABSOLUTE_OUTPUT [CONFIG]'
out=$1
config=${2:-EMBER64}
case "$out" in /*) ;; *) fail 'output must be an absolute path' ;; esac
case "$config" in ''|*[!A-Za-z0-9_]*) fail 'invalid kernel configuration' ;; esac
[ -f "$src/sys/arch/evbarm/conf/$config" ] || fail 'kernel configuration missing'
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd -P)
case "$out/" in //|"$src/"*) fail 'output must be outside the source tree' ;; esac
mkdir "$out/.build-lock" 2>/dev/null || fail 'output is already in use (or has a stale .build-lock)'
stage=
success=no
clear_outputs() {
    rm -f "$out/netbsd-$config" "$out/netbsd-$config.img" \
        "$out/bcm2837-rpi-zero-2-w.dtb" "$out/sun60i-a733-orangepi-zero4.dtb" \
        "$out/sun60i-a733-orangepi-zero3w.dtb"
    for module in if_cemac_acpi bcm2712btcom rp1wmcodec rpi5button; do
        rm -f "$out/$module.kmod"
    done
}
cleanup() {
    test "$success" = yes || clear_outputs
    test -z "$stage" || rm -rf "$stage"
    rmdir "$out/.build-lock"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
# Never expose an earlier candidate or a partially copied result after failure.
clear_outputs
mode=${EMBER_BUILD_MODE:-cross}
if [ "$mode" = auto ]; then
    mode=cross
    if [ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ]; then
        mode=native
    fi
fi
case "$mode" in
native)
    [ "$(uname -s)" = NetBSD ] && [ "$(uname -p)" = aarch64 ] ||
        fail 'native mode requires an AArch64 NetBSD host; use EMBER_BUILD_MODE=cross'
    [ -z "${EMBER_EXTERNAL_TOOLCHAIN:-}" ] || fail 'external tools require cross mode'
    ;;
cross)
    [ -f "$src/tools/Makefile" ] && [ -f "$src/share/mk/bsd.own.mk" ] ||
        fail 'cross mode requires a full source export, including tools and share/mk'
    case ${EMBER_EXTERNAL_TOOLCHAIN:-/} in /*) ;; *) fail 'external toolchain must be an absolute path' ;; esac
    ;;
*) fail 'EMBER_BUILD_MODE must be auto, native or cross' ;;
esac
python=${NETBSD2_PYTHON:-$(command -v python3 || true)}
[ -n "$python" ] && [ -x "$python" ] || fail 'a host Python 3 is required for the existing source contracts; set NETBSD2_PYTHON'
jobs=${NETBSD2_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
case "$jobs" in ''|*[!0-9]*) jobs=0 ;; esac
[ "$jobs" -gt 0 ] 2>/dev/null || fail 'NETBSD2_JOBS must be a positive integer'
stage=$(mktemp -d "$out/.artifacts.XXXXXX")
: > "$out/build.log"
if [ "$mode" = cross ]; then
    # The wrapper retains these directories and target settings for module builds.
    # -u reuses host tools and objects; it does not run target executables on the host.
    set -- -U -u -j"$jobs" -m evbarm -a aarch64 \
        -O "$out/obj" -T "$out/tools" -D "$out/dest" \
        -V "KERNOBJDIR=$out/obj/kernels" -V MKCROSSGDB=no
    if [ -n "${EMBER_EXTERNAL_TOOLCHAIN:-}" ]; then
        set -- "$@" -V "EXTERNAL_TOOLCHAIN=$EMBER_EXTERNAL_TOOLCHAIN" \
            -V TOOLCHAIN_MISSING=yes
    fi
    (cd "$src"; MAKECONF=/dev/null sh ./build.sh "$@" tools "kernel=$config") \
        >> "$out/build.log" 2>&1
    make="$out/tools/bin/nbmake-evbarm"
    kernel_obj="$out/obj/kernels/$config"
    cpp=$("$make" -C "$src" -V '${CPP}')
    dtc=$("$make" -C "$src" -V '${TOOL_DTC}')
else
    make=make
    cpp=cpp
    dtc=dtc
    kernel_obj="$src/sys/arch/evbarm/compile/$config"
    rm -rf "$kernel_obj"
    (cd "$src/sys/arch/evbarm/conf"; config "$config") >> "$out/build.log" 2>&1
    (cd "$kernel_obj"; "$make" -j"$jobs" depend; "$make" -j"$jobs") >> "$out/build.log" 2>&1
fi
cp "$kernel_obj/netbsd" "$stage/netbsd-$config"
cp "$kernel_obj/netbsd.img" "$stage/netbsd-$config.img"
cd "$src/sys/external/gpl2/dts/dist"
"$cpp" -P -xassembler-with-cpp -I include -I arch/arm/boot/dts \
    arch/arm/boot/dts/bcm2837-rpi-zero-2-w.dts > "$out/zero-2.dts"
"$dtc" -I dts -O dtb -p 1024 -b 0 -@ -o "$stage/bcm2837-rpi-zero-2-w.dtb" \
    "$out/zero-2.dts" 2> "$out/zero-2-dtb.log"
for board in orangepi-zero4 orangepi-zero3w; do
    "$cpp" -P -xassembler-with-cpp -I include -I arch/arm64/boot/dts/allwinner \
        "arch/arm64/boot/dts/allwinner/sun60i-a733-$board.dts" > "$out/$board.dts"
    "$dtc" -I dts -O dtb -o "$stage/sun60i-a733-$board.dtb" \
        "$out/$board.dts" 2> "$out/$board-dtb.log"
done
for module in if_cemac_acpi bcm2712btcom rp1wmcodec rpi5button; do
    cd "$src/sys/modules/$module"
    if [ "$mode" = cross ]; then
        "$make" obj >> "$out/build.log" 2>&1
    fi
    "$make" -j"$jobs" EMBER_KERNEL_CONFIG="$config" \
        EMBER_KERNEL_OBJDIR="$kernel_obj" dependall >> "$out/build.log" 2>&1
    module_obj=$("$make" -V .OBJDIR)
    cp "$module_obj/$module.kmod" "$stage/"
done
contracts=all
if [ "$(uname -s)" != NetBSD ]; then
    contracts=portable
fi
NETBSD2_PYTHON="$python" sh "$src/ember/tools/kernel-contracts.sh" "$src" "$contracts" \
    > "$out/contracts.log" 2>&1
cp "$stage/"* "$out/"
success=yes
echo "kernel and four modules built ($mode): $out"
cat "$out/contracts.log"
