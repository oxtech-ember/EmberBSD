#!/bin/sh
# Origin: EmberBSD (AI-assisted), kernel build orchestration regression.
# Exercise build orchestration without substituting these fixtures for a build.
set -eu
# An ambient product version must not replace the selected source version.
export EMBER_VERSION=9.9
# Fixtures select their own build mode and toolchain, independent of the shell.
unset EMBERBSD_SDK_SOURCE EMBER_HOST_PREFIX EMBER_HOST_COMPILER_FAMILY EMBER_BUILD_MODE EMBER_EXTERNAL_TOOLCHAIN EMBER_COMPILER_FAMILY EMBER_LLVM_SOURCE PVR_QUERY_FIRMWARE
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-build-kernel.XXXXXX")
work=$(CDPATH= cd -- "$work" && pwd -P)
show_failure_logs() {
    for log in "$work/run.log" "${TEST_OUT:-$work}/build.log" \
        "${TEST_OUT:-$work}/contracts.log" "$work/commands"; do
        if [ -f "$log" ]; then
            printf '\n%s\n' "$log" >&2
            cat "$log" >&2
        fi
    done
}
cleanup() {
    status=$?
    if [ "$status" -ne 0 ]; then
        show_failure_logs
    fi
    rm -rf "$work"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
export TEST_ROOT="$work" TEST_LOG="$work/commands"
mkdir -p "$work/src/ember/tools" "$work/bin" "$work/src/tools" \
    "$work/src/ember/tests" \
    "$work/src/share/mk" "$work/src/sys/arch/evbarm/conf" \
    "$work/src/sys/external/gpl2/dts/dist"
mkdir -p "$work/llvm/runtimes" "$work/llvm/compiler-rt/lib/builtins"
: > "$work/llvm/runtimes/CMakeLists.txt"
: > "$work/llvm/compiler-rt/lib/builtins/int_lib.h"
export EMBER_LLVM_SOURCE="$work/llvm"
cp "$repo/ember/build-kernel.sh" "$work/src/ember/"
cp "$repo/ember/tools/kernel-contracts.sh" "$work/src/ember/tools/"
# Model only dispatcher arguments and exit status. These fixtures do not
# execute A733, GPU, Ruby, CEMAC, or Wi-Fi production-code contracts.
for contract in a733-accelerator-clock a733-power a733-display-observer a733-gpu-identification \
    a733-gpu-fwinfo a733-gpu-fwimage a733-gpu-mips a733-gpu-fwabi \
    a733-gpu-fwmem a733-gpu-fwinit a733-gpu-fwstart a733-gpu-fwboot \
    a733-gpu-fwprobe a733-gpu-fwcompute a733-gpu-fwfile a733-gpu-mmu \
    a733-gpu-vm pvr-freelist pvr-exec-hwrt a733-gpu-fwbridge \
    a733-gpu-fwservice a733-gpu-fwcompute-vm a733-gpu-fwheap-observe pvr-exec; do
    cat > "$work/src/ember/tools/$contract-contract.sh" <<'SH'
#!/bin/sh
set -eu
echo "a733-contract ${0##*/} $*" >> "$TEST_LOG"
test "$#" -eq 0
test "$CC" = "$TEST_ROOT/bin/host-cc"
test "$CXX" = "$TEST_ROOT/bin/host-cxx"
test "$CFLAGS" = -Dfixture_host
test "$CXXFLAGS" = -Dfixture_cxx
test "$LDFLAGS" = -Lfixture-host
if [ "${FAIL_AT:-}" = a733-contract ] || [ "${FAIL_AT:-}" = "${0##*/}" ]; then
    echo "injected contract failure: ${0##*/}" >&2
    exit 1
fi
SH
done
for contract in bcm2712-display bcm2712-display-irq bcm2712-display-hsm bcm2712-display-dvp \
    bcm2712-v3d bcm2712-v3d-takeover bcm2712-v3d-dma \
    bcm2712-v3d-queue bcm2712-v3d-irq bcm2712-v3d-fault \
    bcm2712-v3d-uapi bcm2712-v3d-bo bcm2712-v3d-sg bcm2712-v3d-job bcm2712-v3d-csd \
    bcm2712-v3d-cl bcm2712-v3d-wait bcm2712-v3d-sync drm-syncobj-wait \
    drm-syncobj-dispatch drm-syncobj-find dma-fence-chain drm-jiffies \
    linux-idr i915-elsq-desc i915-rps-lifetime drm-pci-identity linux-rcu-replace \
    pvr-render pvr-heap pvr-bo pvr-vm pvr-file pvr-stream pvr-context pvr-hwrt pvr-submit pvr-render-attach; do
    cat > "$work/src/ember/tools/$contract-contract.sh" <<'SH'
#!/bin/sh
set -eu
echo "gpu-contract ${0##*/} $*" >> "$TEST_LOG"
test "$#" -eq 1
test "$1" = "$TEST_ROOT/src"
if [ "${FAIL_AT:-}" = gpu-contract ] || [ "${FAIL_AT:-}" = "${0##*/}" ]; then
    echo "injected contract failure: ${0##*/}" >&2
    exit 1
fi
SH
done
cat > "$work/src/ember/tools/pvr-query-contract.sh" <<'SH'
#!/bin/sh
set -eu
echo "pvr-query-contract $*" >> "$TEST_LOG"
test "$#" -eq 1
test "$1" = "$TEST_ROOT/src"
test "$PVR_QUERY_FIRMWARE" = "$TEST_ROOT/pinned firmware"
test -f "$PVR_QUERY_FIRMWARE"
if [ "${FAIL_AT:-}" = pvr-query-contract.sh ]; then
    echo 'injected contract failure: pvr-query-contract.sh' >&2
    exit 1
fi
SH
for contract in cemac-stop bwfm-link-events bwfm-link-check bwfm-sta aicwf-sta aicwf-vht \
    bwfm-scan-padding bwfm-key-epoch; do
cat > "$work/src/ember/tests/$contract.sh" <<'SH'
#!/bin/sh
set -eu
echo "network-contract ${0##*/} $*" >> "$TEST_LOG"
case ${0##*/} in
    aicwf-sta.sh|aicwf-vht.sh) test "$#" -eq 0;;
    *) test "$#" -eq 1; test "$1" = "$TEST_ROOT/src";;
esac
if [ "${FAIL_AT:-}" = network-contract ] || [ "${FAIL_AT:-}" = "${0##*/}" ]; then
    echo "injected contract failure: ${0##*/}" >&2
    exit 1
fi
SH
done
ruby_without_source='bcm2712-v3d-prime simplefb drm-hotplug bcm2712-v3d-bo-stats'
ruby_with_source='drm-gem-fb bcmv3d-resv bcmv3d-read bcmv3d-vblank
bcmv3d-vblank-native bcmv3d-flip bcmv3d-scanout bcmv3d-job-resv
serial-console-ready bootfb-serial-handoff'
for contract in $ruby_without_source $ruby_with_source; do
    : > "$work/src/ember/tools/$contract-contract.rb"
done
cat > "$work/bin/ruby" <<'SCRIPT'
#!/bin/sh
set -eu
echo "ruby-contract $*" >> "$TEST_LOG"
case $1 in "$TEST_ROOT/src/ember/tools/"*-contract.rb) ;; *) exit 1;; esac
case ${1##*/} in
    bcm2712-v3d-prime-contract.rb|simplefb-contract.rb|drm-hotplug-contract.rb|bcm2712-v3d-bo-stats-contract.rb)
        test "$#" -eq 1;;
    drm-gem-fb-contract.rb|bcmv3d-resv-contract.rb|bcmv3d-read-contract.rb|bcmv3d-vblank-contract.rb|bcmv3d-vblank-native-contract.rb|bcmv3d-flip-contract.rb|bcmv3d-scanout-contract.rb|bcmv3d-job-resv-contract.rb|serial-console-ready-contract.rb|bootfb-serial-handoff-contract.rb)
        test "$#" -eq 2
        test "$2" = "$TEST_ROOT/src";;
    *) echo "unexpected Ruby contract: ${1##*/}" >&2; exit 1;;
esac
test -f "$1"
if [ "${FAIL_AT:-}" = ruby-contract ] || [ "${FAIL_AT:-}" = "${1##*/}" ]; then
    echo "injected contract failure: ${1##*/}" >&2
    exit 1
fi
SCRIPT
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
case $1 in
    */scripts/run-native-check.py|*/scripts/run-native-os.py) runner=$1; shift; exec sh "$runner" "$@";;
esac
echo "contract $*" >> "$TEST_LOG"
case $1 in
    */drm-dmabuf-contract.py)
        [ "$#" -eq 2 ] || exit 1
        case $2 in map|lifetime|array|sync) ;; *) exit 1;; esac
        [ "${FAIL_AT:-}" != "dmabuf-$2" ] || exit 1
        ;;
esac
if [ "${TEST_HOST:-Darwin}" != NetBSD ]; then
    case $1 in */btuart-contract.py|*/codec-contract.py|*/bluetooth-control-contract.py|*/bluetooth-pair-contract.py) exit 1;; esac
fi

[ "${FAIL_AT:-}" != contract ]
SH
cat > "$work/src/build.sh" <<'SH'
#!/bin/sh
set -eu
echo "build.sh $*" >> "$TEST_LOG"
[ -z "${EMBER_VERSION:-}" ]
[ "${MAKECONF:-}" = /dev/null ]
[ "${HOST_CC:-}" = "$TEST_ROOT/bin/host-cc" ]
[ "${HOST_CFLAGS:-}" = -Dfixture_host ]
[ -z "${CC:-}" ] && [ -z "${CFLAGS:-}" ]
case " $* " in *" -V HOST_CC=$TEST_ROOT/bin/host-cc "*) ;; *) exit 83;; esac
case " $* " in *" -V HOST_CFLAGS=-Dfixture_host "*) ;; *) exit 84;; esac
for argument do case $argument in BUILDINFO=*) BUILDINFO=${argument#BUILDINFO=};; esac; done
[ "${FAIL_AT:-}" != tools ]
mkdir -p "$TEST_OUT/tools/bin" "$TEST_OUT/obj/kernels/EMBER64"
cp "$TEST_ROOT/bin/nbmake-evbarm" "$TEST_OUT/tools/bin/"
cp "$TEST_ROOT/bin/cpp" "$TEST_OUT/tools/bin/target-cpp"
cp "$TEST_ROOT/bin/cpp" "$TEST_OUT/tools/bin/target cpp"
cp "$TEST_ROOT/bin/dtc" "$TEST_OUT/tools/bin/nbdtc"
# Model make's incremental version object. BUILDINFO is not a dependency.
version_obj="$TEST_OUT/obj/kernels/EMBER64/vers.o"
if [ ! -f "$version_obj" ]; then
    printf '%s\n' "${BUILDINFO:-fixture-kernel}" > "$TEST_OUT/obj/kernels/EMBER64/vers.c"
    cp "$TEST_OUT/obj/kernels/EMBER64/vers.c" "$version_obj"
fi
cp "$version_obj" "$TEST_OUT/obj/kernels/EMBER64/netbsd"
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
        '${HOST_CC}') echo "$TEST_ROOT/bin/host-cc";;
        '${HOST_CXX}') echo "$TEST_ROOT/bin/host-cxx";;
        '${HOST_CFLAGS}') echo -Dfixture_host;;
        '${HOST_CXXFLAGS}') echo -Dfixture_cxx;;
        '${HOST_LDFLAGS}') echo -Lfixture-host;;
        CPP) echo '${TOOL_CPP.${ACTIVE_CPP}}';;
        '${CPP}')
            if [ "${TEST_CPP_SINGLE:-no}" = yes ]; then
                echo "$TEST_OUT/tools/bin/target cpp"
            else
                echo "$TEST_OUT/tools/bin/target-cpp --target=aarch64--netbsd -E"
            fi;;
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
[ -z "${EMBER_VERSION:-}" ]
[ "$1" = -m ] && [ "$2" = "$TEST_ROOT/src/share/mk" ]
shift 2
test "$1" = "EMBER_LLVM_SOURCE=$TEST_ROOT/llvm"
shift
while [ $# -gt 0 ]; do
    case $1 in
    -V) case $2 in
        '${HOST_CC}') echo "$TEST_ROOT/bin/host-cc";;
        '${HOST_CXX}') echo "$TEST_ROOT/bin/host-cxx";;
        '${HOST_CFLAGS}') echo -Dfixture_host;;
        '${HOST_CXXFLAGS}') echo -Dfixture_cxx;;
        '${HOST_LDFLAGS}') echo -Lfixture-host;;
        *) echo "$PWD";;
        esac
        exit;;
    esac
    shift
done
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
case "$*" in
*sun60i-a733-orangepi-zero3w-diagnostic.dtb*)
    [ "${FAIL_AT:-}" != diagnostic-dtb ];;
esac
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
mkdir -p "$work/sdk/scripts"
cat > "$work/sdk/scripts/run-native-check.py" <<'SH'
#!/bin/sh
set -eu
role= log= prefix= family= bindings= cwd= ruby=no
while [ "$#" -gt 0 ]; do
    case $1 in
    --host-prefix) prefix=$2;;
    --compiler-family) family=$2;;
    --required-role) test "$2" = ruby; ruby=yes;;
    --tool) role=$2;;
    --cwd) cwd=$2;;
    --bindings) bindings=$2;;
    --log) log=$2;;
    --) shift; break;;
    *) exit 81;;
    esac
    shift 2
done
test "$prefix" = "$TEST_ROOT"
test "$family" = "${EMBER_HOST_COMPILER_FAMILY:-clang}"
test "$cwd" = "$TEST_ROOT/src"
test "$bindings" = "$TEST_OUT/native-check-commands"
test "$ruby" = yes
echo "native-check $family $role $bindings" >> "$TEST_LOG"
# The real SDK discards consumer interpreter overrides before selecting its tools.
unset NETBSD2_PYTHON
export CC="$TEST_ROOT/bin/host-cc" CXX="$TEST_ROOT/bin/host-cxx"
export CFLAGS=-Dfixture_host CXXFLAGS=-Dfixture_cxx LDFLAGS=-Lfixture-host
# Match process.run_logged: a fresh log is created exclusively.
(set -C; : > "$log")
case $role in
python) "$TEST_ROOT/bin/python3" "$@" > "$log" 2>&1;;
sh) sh "$@" > "$log" 2>&1;;
*) exit 82;;
esac
SH
cat > "$work/sdk/scripts/run-native-os.py" <<'SH'
#!/bin/sh
set -eu
prefix= family= source= routing= cwd= log= capture=no mode=direct append=no
while [ "$#" -gt 0 ]; do
    case $1 in
    --host-prefix) prefix=$2; shift;;
    --compiler-family) family=$2; shift;;
    --source) source=$2; shift;;
    --routing) routing=$2; shift;;
    --cwd) cwd=$2; shift;;
    --log) log=$2; shift;;
    --capture) capture=yes;;
    --append-log) append=yes;;
    --build-sh) mode=build;;
    --shell) mode=shell;;
    --) shift; break;;
    *) exit 85;;
    esac
    shift
done
test "$prefix" = "$TEST_ROOT"
test "$family" = "${EMBER_HOST_COMPILER_FAMILY:-clang}"
test "$source" = "$TEST_ROOT/src"
test "$routing" = "$TEST_OUT/native-build-tools"
mkdir -p "$routing"
for command in sh cp rm rmdir cat mkdir dirname basename uname grep sed awk tr sort touch printf ln pwd; do
    selected=$(command -v "$command")
    case $selected in /*) ;; *)
        for directory in /usr/bin /bin; do
            if [ -x "$directory/$command" ]; then selected="$directory/$command"; break; fi
        done;;
    esac
    test -x "$selected"
    test -e "$routing/$command" || ln -s "$selected" "$routing/$command"
done
export HOST_CC="$TEST_ROOT/bin/host-cc" HOST_CXX="$TEST_ROOT/bin/host-cxx"
export HOST_CFLAGS=-Dfixture_host HOST_CXXFLAGS=-Dfixture_cxx HOST_LDFLAGS=-Lfixture-host
export EMBER_NATIVE_TOOLS="$routing" EMBER_HOST_COMPILER_FAMILY="$family" MAKECONF=/dev/null
unset CC CXX CPP CFLAGS CXXFLAGS CPPFLAGS LDFLAGS BUILDINFO
cd "$cwd"
echo "native-os $family $mode $*" >> "$TEST_LOG"
case $mode in
build) set -- sh "$source/build.sh" -V "HOST_CC=$HOST_CC" -V "HOST_CFLAGS=$HOST_CFLAGS" -V "EMBER_HOST_COMPILER_FAMILY=$family" "$@";;
shell) set -- sh "$@";;
direct) case $1 in /*) ;; *) exit 86;; esac;;
esac
if [ "$capture" = yes ]; then exec "$@"; fi
if [ "$append" = yes ]; then
    "$@" >> "$log" 2>&1
else
    (set -C; : > "$log")
    "$@" > "$log" 2>&1
fi
SH
ln -s python3 "$work/bin/python3.15"
export EMBER_EXTERNAL_TOOLCHAIN=/cross/clang
export EMBERBSD_SDK_SOURCE="$work/sdk" EMBER_HOST_PREFIX="$work"
for tool in host-cc host-cxx; do
    printf '#!/bin/sh\nexit 99\n' > "$work/bin/$tool"
done
chmod +x "$work/bin/"* "$work/src/build.sh"
export PATH="$work/bin:$PATH" NETBSD2_PYTHON="$work/bin/python3" NETBSD2_JOBS=2
export RUBY="$work/bin/ruby"
# Deliberately invalid target commands must not be used by host contracts.
export CC=/target/compiler CXX=/target/compiler-cxx CFLAGS=-Dwrong_target CXXFLAGS=-Dwrong_target LDFLAGS=-Lwrong-target
check_contract_counts() {
    test "$(grep -c '^contract ' "$TEST_LOG")" -eq "$1"
    test "$(grep -c '^a733-contract ' "$TEST_LOG")" -eq 24
    test "$(grep -c '^gpu-contract ' "$TEST_LOG")" -eq 38
    test "$(grep -c '^ruby-contract ' "$TEST_LOG")" -eq 14
    test "$(grep -c '^network-contract ' "$TEST_LOG")" -eq 8
}
export TEST_OUT="$work/cross"
sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
test -s "$TEST_OUT/netbsd-EMBER64"
test -s "$TEST_OUT/rpi5button.kmod"
test -s "$TEST_OUT/sun60i-a733-orangepi-zero3w.dtb"
test -s "$TEST_OUT/sun60i-a733-orangepi-zero3w-diagnostic.dtb"
grep -q 'build.sh .* -m evbarm -a aarch64 ' "$TEST_LOG"
grep -q ' -u ' "$TEST_LOG"
grep -q ' -c clang ' "$TEST_LOG"
grep -Fq -- "-V EMBER_LLVM_SOURCE=$EMBER_LLVM_SOURCE" "$TEST_LOG"
grep -q 'cpp .*/target-cpp --target=aarch64--netbsd -E ' "$TEST_LOG"
grep -q 'EMBER_KERNEL_OBJDIR=.*/obj/kernels/EMBER64' "$TEST_LOG"
grep -q 'cpp .*/tools/bin/target-cpp ' "$TEST_LOG"
grep -q 'dtc .*/tools/bin/nbdtc ' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
check_contract_counts 16
# Reuse all objects, but the selected kernel must embed the new build identity.
printf 'old-source-id\n' > "$TEST_OUT/obj/kernels/EMBER64/vers.c"
printf 'old-object-id\n' > "$TEST_OUT/obj/kernels/EMBER64/vers.o"
printf 'keep-object\n' > "$TEST_OUT/obj/kernels/EMBER64/keep.o"
mkdir -p "$TEST_OUT/obj/kernels/OTHER"
printf 'other-source-id\n' > "$TEST_OUT/obj/kernels/OTHER/vers.c"
printf 'other-object-id\n' > "$TEST_OUT/obj/kernels/OTHER/vers.o"
: > "$TEST_LOG"
BUILDINFO=new-source-id sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
if [ "$(cat "$TEST_OUT/netbsd-EMBER64")" != new-source-id ]; then
    echo 'stale kernel BUILDINFO survived the incremental rebuild' >&2
    exit 1
fi
test "$(cat "$TEST_OUT/obj/kernels/EMBER64/vers.c")" = new-source-id
test "$(cat "$TEST_OUT/obj/kernels/EMBER64/vers.o")" = new-source-id
test "$(cat "$TEST_OUT/obj/kernels/EMBER64/keep.o")" = keep-object
test "$(cat "$TEST_OUT/obj/kernels/OTHER/vers.c")" = other-source-id
test "$(cat "$TEST_OUT/obj/kernels/OTHER/vers.o")" = other-object-id
check_contract_counts 16
# A firmware-dependent contract is pending by default, never silently passed.
! grep -q '^pvr-query-contract ' "$TEST_LOG"
grep -q 'PowerVR metadata query contract pending:' "$TEST_OUT/contracts.log"
# Preserve the existing single-executable CPP spelling, including spaces.
: > "$TEST_LOG"
TEST_CPP_SINGLE=yes sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q 'cpp .*/tools/bin/target cpp -P ' "$TEST_LOG"
: > "$work/pinned firmware"
: > "$TEST_LOG"
CC="$work/bin/host-cc" CXX="$work/bin/host-cxx" CFLAGS=-Dfixture_host CXXFLAGS=-Dfixture_cxx LDFLAGS=-Lfixture-host PVR_QUERY_FIRMWARE="$work/pinned firmware" sh "$work/src/ember/tools/kernel-contracts.sh" \
    "$work/src" portable > "$work/query.log" 2>&1
test "$(grep -c '^pvr-query-contract ' "$TEST_LOG")" -eq 1
! grep -q 'PowerVR metadata query contract pending:' "$work/query.log"
: > "$TEST_LOG"
if CC="$work/bin/host-cc" CXX="$work/bin/host-cxx" CFLAGS=-Dfixture_host CXXFLAGS=-Dfixture_cxx LDFLAGS=-Lfixture-host PVR_QUERY_FIRMWARE="$work/pinned firmware" FAIL_AT=pvr-query-contract.sh \
    sh "$work/src/ember/tools/kernel-contracts.sh" "$work/src" portable \
    > "$work/query.log" 2>&1; then
    echo 'accepted firmware query failure' >&2; exit 1
fi
grep -q 'injected contract failure: pvr-query-contract.sh' "$work/query.log"
! grep -q '^gpu-contract bcm2712-display-contract.sh ' "$TEST_LOG"
outputs='netbsd-EMBER64 netbsd-EMBER64.img bcm2837-rpi-zero-2-w.dtb
sun60i-a733-orangepi-zero4.dtb sun60i-a733-orangepi-zero3w.dtb
sun60i-a733-orangepi-zero3w-diagnostic.dtb
if_cemac_acpi.kmod bcm2712btcom.kmod rp1wmcodec.kmod rpi5button.kmod'
root_argument_failures='linux-idr-contract.sh i915-elsq-desc-contract.sh
i915-rps-lifetime-contract.sh drm-pci-identity-contract.sh linux-rcu-replace-contract.sh
drm-gem-fb-contract.rb bcmv3d-resv-contract.rb bcmv3d-read-contract.rb
bcmv3d-vblank-contract.rb bcmv3d-vblank-native-contract.rb bcmv3d-flip-contract.rb
bcmv3d-scanout-contract.rb bcmv3d-job-resv-contract.rb serial-console-ready-contract.rb
bootfb-serial-handoff-contract.rb'
root_argument_failures_checked=0
for failure in $root_argument_failures tools module cpp dtb diagnostic-dtb contract a733-contract \
    a733-gpu-vm-contract.sh a733-gpu-fwcompute-vm-contract.sh \
    a733-gpu-fwheap-observe-contract.sh a733-gpu-fwbridge-contract.sh \
    a733-gpu-fwservice-contract.sh pvr-exec-contract.sh \
    pvr-render-contract.sh pvr-heap-contract.sh pvr-bo-contract.sh pvr-vm-contract.sh pvr-file-contract.sh pvr-stream-contract.sh pvr-context-contract.sh pvr-hwrt-contract.sh pvr-submit-contract.sh pvr-render-attach-contract.sh \
    gpu-contract bcm2712-display-contract.sh \
    bcm2712-display-irq-contract.sh bcm2712-display-hsm-contract.sh bcm2712-display-dvp-contract.sh dmabuf-map dmabuf-lifetime dmabuf-array dmabuf-sync ruby-contract simplefb-contract.rb drm-hotplug-contract.rb \
    bcm2712-v3d-prime-contract.rb bcm2712-v3d-bo-stats-contract.rb network-contract cemac-stop.sh bwfm-link-check.sh \
    aicwf-sta.sh aicwf-vht.sh publish; do
    : > "$TEST_LOG"
    for output in $outputs; do
        printf stale > "$TEST_OUT/$output"
    done
    if FAIL_AT=$failure sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1; then
        echo "accepted failure: $failure" >&2; exit 1
    fi
    for output in $outputs; do
        test ! -e "$TEST_OUT/$output"
    done
    case "$failure" in
    a733-contract|a733-gpu-vm-contract.sh)
        grep -Fq 'injected contract failure: a733-' "$TEST_OUT/contracts.log"
        ! grep -q '^gpu-contract ' "$TEST_LOG"
        ;;
    a733-gpu-fwcompute-vm-contract.sh|a733-gpu-fwheap-observe-contract.sh|a733-gpu-fwbridge-contract.sh|a733-gpu-fwservice-contract.sh|pvr-*-contract.sh)
        grep -Fq "injected contract failure: $failure" "$TEST_OUT/contracts.log"
        ! grep -q '^gpu-contract bcm2712-display-contract.sh ' "$TEST_LOG"
        ;;
    bcm2712-display-contract.sh|bcm2712-display-irq-contract.sh|bcm2712-display-hsm-contract.sh|bcm2712-display-dvp-contract.sh)
        grep -Fq "gpu-contract $failure " "$TEST_LOG"
        grep -Fq "injected contract failure: $failure" "$TEST_OUT/contracts.log"
        ! grep -q '^gpu-contract bcm2712-v3d-contract.sh ' "$TEST_LOG"
        ;;
    cemac-stop.sh)
        grep -Fq "network-contract $failure " "$TEST_LOG"
        grep -Fq "injected contract failure: $failure" "$TEST_OUT/contracts.log"
        ! grep -q '^network-contract bwfm-link-events.sh ' "$TEST_LOG"
        ;;
    aicwf-sta.sh|aicwf-vht.sh)
        grep -Fq "network-contract $failure " "$TEST_LOG"
        grep -Fq "injected contract failure: $failure" "$TEST_OUT/contracts.log"
        ! grep -q '^network-contract bwfm-scan-padding.sh ' "$TEST_LOG"
        ;;
    esac
    for root_failure in $root_argument_failures; do
        [ "$failure" = "$root_failure" ] || continue
        case $failure in
        *.rb) expected="ruby-contract $work/src/ember/tools/$failure $work/src";;
        *.sh) expected="gpu-contract $failure $work/src";;
        esac
        test "$(tail -n 1 "$TEST_LOG")" = "$expected"
        grep -Fq "injected contract failure: $failure" "$TEST_OUT/contracts.log"
        root_argument_failures_checked=$((root_argument_failures_checked + 1))
    done
done
test "$root_argument_failures_checked" -eq 15
echo "PASS: $root_argument_failures_checked explicit-root contract failures stop dispatch and clear outputs"
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_COMPILER_FAMILY=gcc EMBER_EXTERNAL_TOOLCHAIN=/cross/gcc16 \
    sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q 'EXTERNAL_TOOLCHAIN=/cross/gcc16' "$TEST_LOG"
grep -q ' -c gcc ' "$TEST_LOG"
grep -q 'TOOLCHAIN_MISSING=yes' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
check_contract_counts 20
export TEST_OUT="$work/native"
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_BUILD_MODE=native sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
! grep -q native-make "$TEST_LOG"
grep -q build.sh "$TEST_LOG"
test -s "$TEST_OUT/bcm2712btcom.kmod"
check_contract_counts 20
if EMBER_BUILD_MODE=native sh "$work/src/ember/build-kernel.sh" "$work/wrong-host" > "$work/run.log" 2>&1; then
    echo 'accepted native tools on Darwin' >&2; exit 1
fi
if EMBER_COMPILER_FAMILY=unknown sh "$work/src/ember/build-kernel.sh" "$work/bad-family" > "$work/run.log" 2>&1; then
    echo 'accepted unknown compiler family' >&2; exit 1
fi
grep -q 'EMBER_COMPILER_FAMILY' "$work/run.log"
for llvm in '' relative "$work/missing-llvm" "$work/bad source"; do
    : > "$TEST_LOG"
    if EMBER_LLVM_SOURCE="$llvm" sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1; then
        echo 'accepted missing or invalid common LLVM sources' >&2; exit 1
    fi
    grep -q EMBER_LLVM_SOURCE "$work/run.log"
    ! grep -q '^build.sh ' "$TEST_LOG"
    test ! -e "$TEST_OUT/netbsd-EMBER64"
done
for invalid in sdk prefix family target; do
    : > "$TEST_LOG"
    if (
        case $invalid in
        sdk) EMBERBSD_SDK_SOURCE="$work/missing-sdk"; export EMBERBSD_SDK_SOURCE;;
        prefix) EMBER_HOST_PREFIX=relative; export EMBER_HOST_PREFIX;;
        family) EMBER_HOST_COMPILER_FAMILY=unknown; export EMBER_HOST_COMPILER_FAMILY;;
        target) EMBER_EXTERNAL_TOOLCHAIN=; export EMBER_EXTERNAL_TOOLCHAIN;;
        esac
        sh "$work/src/ember/build-kernel.sh" "$TEST_OUT"
    ) > "$work/run.log" 2>&1; then
        echo "accepted invalid native-check input: $invalid" >&2; exit 1
    fi
    ! grep -q '^build.sh ' "$TEST_LOG"
    test ! -e "$TEST_OUT/netbsd-EMBER64"
done
export TEST_OUT="$work/host-gcc"
: > "$TEST_LOG"
NETBSD2_PYTHON=/outside/bin/python3 PVR_QUERY_FIRMWARE="$work/pinned firmware" EMBER_HOST_COMPILER_FAMILY=gcc \
    sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q '^native-check gcc sh ' "$TEST_LOG"
grep -q ' -c clang ' "$TEST_LOG"
grep -q '^pvr-query-contract ' "$TEST_LOG"
check_contract_counts 16
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
echo 'PASS: cross/native routing, refreshed kernel identity, retained objects, matched module headers, external tools, base/A733/GPU/Ruby/CEMAC/Wi-Fi contract dispatch, failure cleanup'
