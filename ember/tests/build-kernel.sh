#!/bin/sh
# Origin: EmberBSD (AI-assisted), kernel build orchestration regression.
# Exercise build orchestration without substituting these fixtures for a build.
set -eu
# Fixtures select their own build mode and toolchain, independent of the shell.
unset EMBER_BUILD_MODE EMBER_EXTERNAL_TOOLCHAIN
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
cp "$repo/ember/build-kernel.sh" "$work/src/ember/"
cp "$repo/ember/tools/kernel-contracts.sh" "$work/src/ember/tools/"
# Model only dispatcher arguments and exit status. These fixtures do not
# execute A733, GPU, Ruby, CEMAC, or Wi-Fi production-code contracts.
for contract in a733-accelerator-clock a733-power a733-display-observer a733-gpu-identification \
    a733-gpu-fwinfo a733-gpu-fwimage a733-gpu-mips a733-gpu-fwabi \
    a733-gpu-fwmem a733-gpu-fwinit a733-gpu-fwstart a733-gpu-fwboot \
    a733-gpu-fwprobe a733-gpu-fwcompute a733-gpu-fwfile a733-gpu-mmu \
    a733-gpu-vm pvr-freelist pvr-exec-hwrt a733-gpu-fwbridge \
    a733-gpu-fwservice; do
    cat > "$work/src/ember/tools/$contract-contract.sh" <<'SH'
#!/bin/sh
set -eu
echo "a733-contract ${0##*/} $*" >> "$TEST_LOG"
test "$#" -eq 0
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
    drm-syncobj-dispatch drm-syncobj-find dma-fence-chain drm-jiffies; do
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
for contract in drm-prime-map drm-dmabuf-lifetime bcm2712-v3d-prime simplefb drm-hotplug bcm2712-v3d-bo-stats; do
    : > "$work/src/ember/tools/$contract-contract.rb"
done
cat > "$work/bin/ruby" <<'SCRIPT'
#!/bin/sh
set -eu
echo "ruby-contract $*" >> "$TEST_LOG"
test "$#" -eq 1
case $1 in "$TEST_ROOT/src/ember/tools/"*-contract.rb) ;; *) exit 1;; esac
test -f "$1"
test "${FAIL_AT:-}" != ruby-contract
test "${FAIL_AT:-}" != "${1##*/}"
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
export RUBY="$work/bin/ruby"
check_contract_counts() {
    test "$(grep -c '^contract ' "$TEST_LOG")" -eq "$1"
    test "$(grep -c '^a733-contract ' "$TEST_LOG")" -eq 21
    test "$(grep -c '^gpu-contract ' "$TEST_LOG")" -eq 23
    test "$(grep -c '^ruby-contract ' "$TEST_LOG")" -eq 6
    test "$(grep -c '^network-contract ' "$TEST_LOG")" -eq 8
}
export TEST_OUT="$work/cross"
sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
test -s "$TEST_OUT/netbsd-EMBER64"
test -s "$TEST_OUT/rpi5button.kmod"
test -s "$TEST_OUT/sun60i-a733-orangepi-zero3w.dtb"
grep -q 'build.sh .* -m evbarm -a aarch64 ' "$TEST_LOG"
grep -q ' -u ' "$TEST_LOG"
grep -q 'EMBER_KERNEL_OBJDIR=.*/obj/kernels/EMBER64' "$TEST_LOG"
grep -q 'cpp .*/tools/bin/target-cpp ' "$TEST_LOG"
grep -q 'dtc .*/tools/bin/nbdtc ' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
check_contract_counts 3
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
check_contract_counts 3
outputs='netbsd-EMBER64 netbsd-EMBER64.img bcm2837-rpi-zero-2-w.dtb
sun60i-a733-orangepi-zero4.dtb sun60i-a733-orangepi-zero3w.dtb
if_cemac_acpi.kmod bcm2712btcom.kmod rp1wmcodec.kmod rpi5button.kmod'
for failure in tools module cpp dtb contract a733-contract \
    a733-gpu-vm-contract.sh gpu-contract bcm2712-display-contract.sh \
    bcm2712-display-irq-contract.sh bcm2712-display-hsm-contract.sh bcm2712-display-dvp-contract.sh ruby-contract simplefb-contract.rb drm-hotplug-contract.rb \
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
done
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_EXTERNAL_TOOLCHAIN=/cross/gcc16 \
    sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q 'EXTERNAL_TOOLCHAIN=/cross/gcc16' "$TEST_LOG"
grep -q 'TOOLCHAIN_MISSING=yes' "$TEST_LOG"
! grep -q native-make "$TEST_LOG"
check_contract_counts 7
export TEST_OUT="$work/native"
: > "$TEST_LOG"
TEST_HOST=NetBSD EMBER_BUILD_MODE=native sh "$work/src/ember/build-kernel.sh" "$TEST_OUT" > "$work/run.log" 2>&1
grep -q native-make "$TEST_LOG"
! grep -q build.sh "$TEST_LOG"
test -s "$TEST_OUT/bcm2712btcom.kmod"
check_contract_counts 7
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
echo 'PASS: cross/native routing, refreshed kernel identity, retained objects, matched module headers, external tools, base/A733/GPU/Ruby/CEMAC/Wi-Fi contract dispatch, failure cleanup'
