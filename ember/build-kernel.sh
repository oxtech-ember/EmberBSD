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
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || fail 'usage: build-kernel.sh ABSOLUTE_OUTPUT [CONFIG | ABSOLUTE_CONFIG_PATH]'
out=$1
# CONFIG names a configuration of sys/arch/evbarm/conf. A local trial
# configuration is given by its absolute path.
config_arg=${2:-EMBER64}
# The board modules built against each kernel.
modules='bcm2712btcom bcmexpgpio rp1wmcodec rpi5button'
config=${config_arg##*/}
case "$out" in /*) ;; *) fail 'output must be an absolute path' ;; esac
case "$config" in ''|*[!A-Za-z0-9_]*) fail 'invalid kernel configuration' ;; esac
case "$config_arg" in
/*)
    case "$config_arg" in *[!A-Za-z0-9_./+@%=-]*) fail 'invalid kernel configuration path' ;; esac
    # The kernel objects and outputs are named by the configuration name.
    [ ! -e "$src/sys/arch/evbarm/conf/$config" ] ||
        fail "the external configuration has the name of the OS configuration $config"
    [ -f "$config_arg" ] || fail 'kernel configuration missing' ;;
*/*) fail 'give a configuration name or an absolute configuration path' ;;
*) [ -f "$src/sys/arch/evbarm/conf/$config" ] || fail 'kernel configuration missing' ;;
esac
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
        "$out/sun60i-a733-orangepi-zero3w-diagnostic.dtb" \
        "$out/sun60i-a733-orangepi-zero4-graphics.dtb" \
        "$out/sun50i-a133-ys-m33.dtb"
    # Also remove if_cemac_acpi.kmod: EMBER64 has CEMAC built in.
    for module in if_cemac_acpi $modules; do
        rm -f "$out/$module.kmod"
    done
}
cleanup() {
    test "$success" = yes || clear_outputs
    test -z "$stage" || rm -rf "$stage"
    rmdir "$out/.build-lock"
}
trap cleanup EXIT
# mksh can skip the EXIT trap after a signal trap. Clean up here.
trap 'cleanup; trap - EXIT; exit 1' HUP INT TERM
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
# The host yacc in tools builds from the byacc source prepared by Ports.
byacc_source=${EMBER_BYACC_SOURCE:-}
case "$byacc_source" in
/*) case "$byacc_source" in *[!A-Za-z0-9_./+@%=-]*) fail 'invalid EMBER_BYACC_SOURCE path';; esac;;
*) fail 'set EMBER_BYACC_SOURCE to the absolute Ports-prepared byacc source';;
esac
[ -f "$byacc_source/emberbsd-prepared-source.json" ] && [ -f "$byacc_source/main.c" ] ||
    fail 'EMBER_BYACC_SOURCE does not contain the prepared byacc source'
# The tools phase builds the host ASN.1 compiler from the common Heimdal source.
heimdal_source=${EMBER_HEIMDAL_SOURCE:-}
case "$heimdal_source" in
/*) case "$heimdal_source" in *[!A-Za-z0-9_./+@%=-]*) fail 'invalid EMBER_HEIMDAL_SOURCE path';; esac;;
*) fail 'set EMBER_HEIMDAL_SOURCE to the absolute Ports-prepared Heimdal source';;
esac
[ -f "$heimdal_source/emberbsd-prepared-source.json" ] && [ -d "$heimdal_source/lib/asn1" ] ||
    fail 'EMBER_HEIMDAL_SOURCE does not contain the prepared Heimdal source'
# A kernel with libz reads the prepared common zlib source of the base
# system. The EMBER kernels do not; ember/shared-sources.json names the
# configurations that do.
zlib_source=${EMBER_ZLIB_SOURCE:-}
case "$zlib_source" in
'') ;;
/*) case "$zlib_source" in *[!A-Za-z0-9_./+@%=-]*) fail 'invalid EMBER_ZLIB_SOURCE path';; esac
    for input in zlib.h zconf.h zutil.h inflate.c inftrees.c emberbsd-prepared-source.json; do
        [ -f "$zlib_source/$input" ] ||
            fail "EMBER_ZLIB_SOURCE is missing prepared zlib input: $input"
    done;;
*) fail 'EMBER_ZLIB_SOURCE must be the absolute Ports-prepared zlib source';;
esac
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
        --cppflag="-I$host_prefix/include" --ldflag="-L$host_prefix/lib" \
        --ldflag="-Wl,-rpath,$host_prefix/lib" \
        --tool "$role" --cwd "$src" --bindings "$out/native-check-commands" \
        --log "$log" -- "$@"
}
jobs=$(PYTHONPATH="$sdk_source/src" "$python" -B -c '
import os, sys
from emberbsd_sdk.cpu_capacity import resolve_jobs
from emberbsd_sdk.errors import SDKError
try:
    print(resolve_jobs(os.environ.get("NETBSD2_JOBS"), option="NETBSD2_JOBS"))
except SDKError as exc:
    print(exc, file=sys.stderr)
    sys.exit(2)
') || fail 'cannot select build parallelism; set NETBSD2_JOBS if CPU capacity is unavailable'
# These caller-owned bindings contain generated routing, not build objects.
# Refresh them once under .build-lock, because the selected flags or the OS
# native-tool contract can change between invocations. The SDK still
# validates their reuse within this invocation.
rm -rf "$out/native-check-commands" "$out/native-build-tools"
stage=$(mktemp -d "$out/.artifacts.XXXXXX")
: > "$out/build.log"
kernel_obj="$out/obj/kernels/$config"
# MKREPRO removes host paths, user, host, build number and build time.
# Use MKREPRO_TIMESTAMP if set. Else use the time of the source commit.
repro_timestamp=${MKREPRO_TIMESTAMP:-}
if [ -z "$repro_timestamp" ]; then
    git_top=$(git -C "$src" rev-parse --show-toplevel) || git_top=
    [ "$git_top" = "$src" ] ||
        fail 'set MKREPRO_TIMESTAMP or build from a Git checkout'
    repro_timestamp=$(git -c log.showSignature=false -C "$src" log -1 --format=%ct HEAD) ||
        fail 'cannot read the source commit time; set MKREPRO_TIMESTAMP'
fi
case $repro_timestamp in ''|*[!0-9]*) fail 'invalid MKREPRO_TIMESTAMP';; esac
# build.sh handles both a native build host and cross compilation. Its generated
# wrapper retains target settings and the shared runner's native HOST_* flags.
set -- -U -u -j"$jobs" -c "$compiler_family" -m evbarm -a aarch64 \
    -O "$out/obj" -T "$out/tools" -D "$out/dest" \
    -V "KERNOBJDIR=$out/obj/kernels" -V MKCROSSGDB=no \
    -V "EMBER_LLVM_SOURCE=$llvm_source" -V "EMBER_BYACC_SOURCE=$byacc_source" \
    -V "EMBER_HEIMDAL_SOURCE=$heimdal_source" \
    -V "EXTERNAL_TOOLCHAIN=$EMBER_EXTERNAL_TOOLCHAIN" -V TOOLCHAIN_MISSING=yes \
    -V MKREPRO=yes -V "MKREPRO_TIMESTAMP=$repro_timestamp"
case ${EMBER_HOST_BFD_CONFIG:-} in
/*) [ -f "$EMBER_HOST_BFD_CONFIG" ] || fail 'common host BFD configuration missing';;
*) fail 'EMBER_HOST_BFD_CONFIG must be an absolute file path';;
esac
set -- "$@" -V "EMBER_HOST_BFD_CONFIG=$EMBER_HOST_BFD_CONFIG"
if [ -n "$zlib_source" ]; then set -- "$@" -V "EMBER_ZLIB_SOURCE=$zlib_source"; fi
if [ -n "${BUILDINFO:-}" ]; then set -- "$@" -V "BUILDINFO=$BUILDINFO"; fi
# newvers.mk does not track BUILDINFO or MKREPRO_TIMESTAMP under -u. Remember
# the values accepted by the previous successful kernel build and invalidate
# only the version object when they change (or its outputs are missing).
buildinfo=${BUILDINFO:-}
version_record="$kernel_obj/.ember-version-inputs"
# Objects of a build without this record can hold host paths, and make -u
# does not rebuild objects for new flags. Remove the object directory one
# time. An empty record prevents a second removal after a failed build.
if [ ! -f "$version_record" ]; then
    rm -rf "$kernel_obj"
    mkdir -p "$kernel_obj"
    : > "$version_record"
fi
previous_version=
if [ -f "$version_record" ]; then
    previous_version=$(cat "$version_record"; printf 'x')
fi
current_version=$(printf '%s\n%s' "$repro_timestamp" "$buildinfo"; printf 'x')
if [ ! -f "$version_record" ] || [ "$previous_version" != "$current_version" ] ||
    [ ! -f "$kernel_obj/vers.c" ] || [ ! -f "$kernel_obj/vers.o" ]; then
    rm -f "$kernel_obj/vers.c" "$kernel_obj/vers.o"
fi
(cd "$src"; run_native_os --log "$out/build.log" --append-log --build-sh -- "$@" tools "kernel=$config_arg")
printf '%s\n%s' "$repro_timestamp" "$buildinfo" > "$version_record.new"
mv -f "$version_record.new" "$version_record"
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
for board in orangepi-zero4 orangepi-zero3w orangepi-zero3w-diagnostic orangepi-zero4-graphics; do
    run_cpp -P -xassembler-with-cpp -I include -I arch/arm64/boot/dts/allwinner \
        "arch/arm64/boot/dts/allwinner/sun60i-a733-$board.dts" > "$out/$board.dts"
    run_native_os --capture -- "$dtc" -I dts -O dtb -o "$stage/sun60i-a733-$board.dtb" \
        "$out/$board.dts" 2> "$out/$board-dtb.log"
done
run_cpp -P -xassembler-with-cpp -I include -I arch/arm64/boot/dts/allwinner \
    arch/arm64/boot/dts/allwinner/sun50i-a133-ys-m33.dts > "$out/a133-ys-m33.dts"
run_native_os --capture -- "$dtc" -I dts -O dtb -o "$stage/sun50i-a133-ys-m33.dtb" \
    "$out/a133-ys-m33.dts" 2> "$out/a133-ys-m33-dtb.log"
run_native_check python "$out/dtb-modes.log" \
    "$src/ember/tools/a733-boot-modes-contract.py" "$stage"
for module in $modules; do
    cd "$src/sys/modules/$module"
    run_make_build obj
    run_make_build -j"$jobs" EMBER_KERNEL_CONFIG="$config" \
        EMBER_KERNEL_OBJDIR="$kernel_obj" dependall
    module_obj=$(run_make -V .OBJDIR)
    cp "$module_obj/$module.kmod" "$stage/"
done
# The SDK build plan reads the prepared sources of a kernel from
# ember/shared-sources.json. Stop if this kernel and its modules register
# other sources (REPRO_EXTERNAL_SOURCE_VARS) than that file declares.
# Query with the sources that the build had. The paths have no spaces.
selected="EMBER_BYACC_SOURCE=$byacc_source EMBER_HEIMDAL_SOURCE=$heimdal_source"
[ -z "$zlib_source" ] || selected="$selected EMBER_ZLIB_SOURCE=$zlib_source"
set -f
registered=$(cd "$kernel_obj" && run_make -V REPRO_EXTERNAL_SOURCE_VARS $selected)
for module in $modules; do
    registered="$registered $(cd "$src/sys/modules/$module" && run_make -V REPRO_EXTERNAL_SOURCE_VARS \
        EMBER_KERNEL_CONFIG="$config" EMBER_KERNEL_OBJDIR="$kernel_obj" $selected)"
done
run_native_check python "$out/shared-sources.log" \
    "$src/ember/tools/shared-sources.py" check-kernel "$src/ember/shared-sources.json" \
    "$config" $registered
set +f
cp "$stage/"* "$out/"
success=yes
echo "kernel and modules built ($mode): $out"
echo "check the kernel sources: ember/check-kernel.sh $out [BASE_COMMIT]"
