#!/bin/sh
# Build the AArch64 kernel, matched modules and board device trees.
set -eu
unset EMBER_VERSION
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
fail() { echo "$*" >&2; exit 1; }
# Use settings from the selected source tree, including its product version.
run_make() { run_native_os --capture --shell -- "$make" -m "$src/share/mk" "EMBER_LLVM_SOURCE=$llvm_source" "$@"; }
run_make_build() { run_native_os --log "$out/build.log" --append-log --shell -- "$make" -m "$src/share/mk" "EMBER_LLVM_SOURCE=$llvm_source" "$@"; }
# Make returns an executable and fixed compiler arguments for CPP.
# Toolchain paths do not contain whitespace. Do not evaluate shell syntax.
run_cpp() (
    if [ -x "$cpp" ]; then
        run_native_os --capture -- "$cpp" "$@"
        exit
    fi
    set -f
    set -- $cpp "$@"
    run_native_os --capture -- "$@"
)
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
        "$out/sun60i-a733-orangepi-zero3w.dtb" \
        "$out/sun60i-a733-orangepi-zero3w-diagnostic.dtb"
    for module in if_cemac_acpi bcm2712btcom bcmexpgpio rp1wmcodec rpi5button; do
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
llvm_source=${EMBER_LLVM_SOURCE:-}
case "$llvm_source" in
/*) case "$llvm_source" in *[!A-Za-z0-9_./+@%=-]*) fail 'invalid EMBER_LLVM_SOURCE path';; esac;;
*) fail 'set EMBER_LLVM_SOURCE to the absolute Ports-prepared LLVM root';;
esac
[ -f "$llvm_source/runtimes/CMakeLists.txt" ] &&
    [ -f "$llvm_source/compiler-rt/lib/builtins/int_lib.h" ] ||
    fail 'EMBER_LLVM_SOURCE does not contain the prepared compiler-rt sources'
export EMBER_LLVM_SOURCE
mode=${EMBER_BUILD_MODE:-cross}
compiler_family=${EMBER_COMPILER_FAMILY:-clang}
case "$compiler_family" in
clang|gcc) ;;
*) fail 'EMBER_COMPILER_FAMILY must be clang or gcc' ;;
esac
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
    ;;
cross) ;;
*) fail 'EMBER_BUILD_MODE must be auto, native or cross' ;;
esac
[ -f "$src/tools/Makefile" ] && [ -f "$src/share/mk/bsd.own.mk" ] ||
    fail 'kernel builds require a full source export, including tools and share/mk'
case ${EMBER_EXTERNAL_TOOLCHAIN:-} in
/*) ;;
*) fail 'set EMBER_EXTERNAL_TOOLCHAIN to the absolute selected target compiler prefix';;
esac
sdk_source=${EMBERBSD_SDK_SOURCE:-}
host_prefix=${EMBER_HOST_PREFIX:-}
host_family=${EMBER_HOST_COMPILER_FAMILY:-clang}
case "$sdk_source" in /*) ;; *) fail 'set EMBERBSD_SDK_SOURCE to the SDK source root';; esac
case "$host_prefix" in /*) ;; *) fail 'set EMBER_HOST_PREFIX to the selected native prefix';; esac
case "$host_family" in clang|gcc) ;; *) fail 'EMBER_HOST_COMPILER_FAMILY must be clang or gcc';; esac
native_check="$sdk_source/scripts/run-native-check.py"
[ -f "$native_check" ] || fail 'SDK source lacks the shared native-check runner'
native_os="$sdk_source/scripts/run-native-os.py"
[ -f "$native_os" ] || fail 'SDK source lacks the shared native-OS runner'
python="$host_prefix/bin/python3.15"
[ -x "$python" ] || fail 'selected prefix lacks its Python 3.15'
run_native_os() {
    "$python" "$native_os" --host-prefix "$host_prefix" \
        --compiler-family "$host_family" --source "$src" \
        --routing "$out/native-build-tools" --cwd "$(pwd -P)" "$@"
}
run_native_check() {
    role=$1
    log=$2
    shift 2
    # Preserve the wrapper's overwrite behavior while the SDK creates the
    # current check log exclusively. Only our two named output logs reach here.
    rm -f "$log"
    "$python" "$native_check" --host-prefix "$host_prefix" \
        --compiler-family "$host_family" --required-role ruby --required-role awk \
        --tool "$role" --cwd "$src" --bindings "$out/native-check-commands" \
        --log "$log" -- "$@"
}
jobs=${NETBSD2_JOBS:-$("$python" -c 'import os; print(os.cpu_count() or 1)')}
case "$jobs" in ''|*[!0-9]*) jobs=0 ;; esac
[ "$jobs" -gt 0 ] 2>/dev/null || fail 'NETBSD2_JOBS must be a positive integer'
stage=$(mktemp -d "$out/.artifacts.XXXXXX")
: > "$out/build.log"
kernel_obj="$out/obj/kernels/$config"
# newvers.mk does not track BUILDINFO as a dependency under -u.
# Refresh only this kernel's identity and retain other objects.
rm -f "$kernel_obj/vers.c" "$kernel_obj/vers.o"
# build.sh handles both a native build host and cross compilation. Its generated
# wrapper retains target settings and the shared runner's native HOST_* flags.
set -- -U -u -j"$jobs" -c "$compiler_family" -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/dest" \
    -V "KERNOBJDIR=$out/obj/kernels" -V MKCROSSGDB=no \
    -V "EMBER_LLVM_SOURCE=$llvm_source" \
    -V "EXTERNAL_TOOLCHAIN=$EMBER_EXTERNAL_TOOLCHAIN" -V TOOLCHAIN_MISSING=yes
case ${EMBER_HOST_BFD_CONFIG:-} in
/*) [ -f "$EMBER_HOST_BFD_CONFIG" ] || fail 'common host BFD configuration missing';;
*) fail 'EMBER_HOST_BFD_CONFIG must be an absolute file path';;
esac
set -- "$@" -V "EMBER_HOST_BFD_CONFIG=$EMBER_HOST_BFD_CONFIG"
if [ -n "${BUILDINFO:-}" ]; then set -- "$@" -V "BUILDINFO=$BUILDINFO"; fi
(cd "$src"; run_native_os --log "$out/build.log" --append-log --build-sh -- "$@" tools "kernel=$config")
# Use the same selected command mapping for subsequent orchestration utilities.
PATH="$out/native-build-tools:$host_prefix/bin:$host_prefix/sbin:$host_prefix/gnu/bin"
export PATH
make="$out/tools/bin/nbmake-evbarm"
cpp=$(run_make -C "$src" -V '${CPP}')
dtc=$(run_make -C "$src" -V '${TOOL_DTC}')
cp "$kernel_obj/netbsd" "$stage/netbsd-$config"
cp "$kernel_obj/netbsd.img" "$stage/netbsd-$config.img"
cd "$src/sys/external/gpl2/dts/dist"
run_cpp -P -xassembler-with-cpp -I include -I arch/arm/boot/dts \
    arch/arm/boot/dts/bcm2837-rpi-zero-2-w.dts > "$out/zero-2.dts"
run_native_os --capture -- "$dtc" -I dts -O dtb -p 1024 -b 0 -@ -o "$stage/bcm2837-rpi-zero-2-w.dtb" \
    "$out/zero-2.dts" 2> "$out/zero-2-dtb.log"
for board in orangepi-zero4 orangepi-zero3w orangepi-zero3w-diagnostic; do
    run_cpp -P -xassembler-with-cpp -I include -I arch/arm64/boot/dts/allwinner \
        "arch/arm64/boot/dts/allwinner/sun60i-a733-$board.dts" > "$out/$board.dts"
    run_native_os --capture -- "$dtc" -I dts -O dtb -o "$stage/sun60i-a733-$board.dtb" \
        "$out/$board.dts" 2> "$out/$board-dtb.log"
done
run_native_check python "$out/dtb-modes.log" \
    "$src/ember/tools/a733-boot-modes-contract.py" "$stage"
for module in if_cemac_acpi bcm2712btcom bcmexpgpio rp1wmcodec rpi5button; do
    cd "$src/sys/modules/$module"
    run_make_build obj
    run_make_build -j"$jobs" EMBER_KERNEL_CONFIG="$config" \
        EMBER_KERNEL_OBJDIR="$kernel_obj" dependall
    module_obj=$(run_make -V .OBJDIR)
    cp "$module_obj/$module.kmod" "$stage/"
done
contracts=all
if [ "$(uname -s)" != NetBSD ]; then
    contracts=portable
fi
# The common SDK runner binds native compiler and ABI arguments even for
# existing contracts that accept only an executable CC and ignore CFLAGS.
run_native_check sh "$out/contracts.log" "$src/ember/tools/kernel-contracts.sh" \
    "$src" "$contracts" "${PVR_QUERY_FIRMWARE:-}"
cp "$stage/"* "$out/"
success=yes
echo "kernel and five modules built ($mode): $out"
cat "$out/contracts.log"
