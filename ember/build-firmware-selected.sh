#!/usr/bin/env bash
# Build the pinned eotics UEFI with this fork's ordered hardware diffs.
# Internal selected-toolchain firmware build implementation.
# No containers, no per-run network package installs, no cloud steps.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# --prepare verifies and updates pinned sources without a target compiler.
prepare_only=0
args=()
for arg in "$@"; do
    [[ "$arg" == "--prepare" ]] && prepare_only=1 || args+=("$arg")
done
if [[ ${#args[@]} -eq 0 ]]; then set --; else set -- "${args[@]}"; fi
[[ $# -ge 1 && $# -le 2 ]] || { echo 'usage: build-firmware.sh CACHE [--rp1-console|--rp1-trace] [--prepare]' >&2; exit 1; }
case "$1" in /*) CACHE="$1" ;; *) echo 'CACHE must be an absolute path' >&2; exit 1 ;; esac
revision=246e70acd939d62b5a79f895984afd0a93bd83e9
repo=https://github.com/eotics-com/rpi5-uefi.git
variant_flags=(--model 5)
variant=
case "${2:-}" in
    '') ;;
    --rp1-console) variant=-rp1-console; variant_flags+=(--tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE') ;;
    --rp1-trace)   variant=-rp1-trace;   variant_flags+=(--tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE -D CM5_BOOT_TRACE=TRUE') ;;
    *) echo 'unknown firmware option' >&2; exit 1 ;;
esac
# The pinned top-level build API splits its flag strings on whitespace.
for input_path in "$CACHE" "$ROOT"; do
    [[ "$input_path" != *[[:space:]]* ]] || {
        echo 'firmware source and cache paths must not contain whitespace' >&2; exit 1;
    }
done
if [[ $prepare_only -eq 0 ]]; then
    for input_path in "${CROSS_COMPILE:-}" "${EMBERBSD_SDK_SOURCE:-}" "${EMBER_HOST_PREFIX:-}" "${EMBER_TARGET_PREFIX:-}" "${TMPDIR:-}"; do
        [[ "$input_path" != *[[:space:]]* ]] || {
            echo 'explicit firmware toolchain paths must not contain whitespace' >&2; exit 1;
        }
    done
fi
# Lease identity needs these before a cache lock is acquired.
for tool in ls awk; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing host tool: $tool" >&2; exit 1; }
done
awk 'BEGIN { exit 0 }' </dev/null || { echo 'unusable host tool: awk' >&2; exit 1; }
mkdir -p "$CACHE"
CACHE="$(cd "$CACHE" && pwd -P)"
out="$CACHE/firmware$variant"
src="$CACHE/rpi5-uefi-src"
# One lease covers preparation, build, the upstream SD-patch EXIT trap,
# source postcheck and publication. Never steal a stale lease.
firmware_lock="$CACHE/.ember-firmware.lock"
mkdir "$firmware_lock" 2>/dev/null || {
    echo 'firmware cache is locked; preserve and inspect any stale lease' >&2; exit 1;
}
firmware_empty_root=
firmware_source_work=
firmware_initializing=1
if ! firmware_lock_inode=$(ls -di "$firmware_lock" | awk '{print $1}') ||
    [[ ! "$firmware_lock_inode" =~ ^[0-9]+$ ]]; then
    echo 'cannot identify firmware cache lease' >&2
    rmdir "$firmware_lock" || echo 'cannot release empty firmware cache lease' >&2
    exit 1
fi
firmware_owns_lease()
{
    [[ ! -L "$firmware_lock" && -d "$firmware_lock" && \
        -f "$firmware_lock/owner" && ! -L "$firmware_lock/owner" && \
        -f "$firmware_lock/source" && ! -L "$firmware_lock/source" && \
        $(cat "$firmware_lock/owner") == "$$" && \
        $(cat "$firmware_lock/source") == "$src" ]]
}
firmware_original_lease()
{
    # A failed release must still block new owners before invalidation.
    [[ -d "$firmware_lock" && ! -L "$firmware_lock" && \
        $(ls -di "$firmware_lock" | awk '{print $1}') == "$firmware_lock_inode" ]] || return 1
    if [[ -e "$firmware_lock/owner" || -L "$firmware_lock/owner" ]]; then
        [[ -f "$firmware_lock/owner" && ! -L "$firmware_lock/owner" && \
            $(cat "$firmware_lock/owner") == "$$" ]] || return 1
    fi
    if [[ -e "$firmware_lock/source" || -L "$firmware_lock/source" ]]; then
        [[ -f "$firmware_lock/source" && ! -L "$firmware_lock/source" && \
            $(cat "$firmware_lock/source") == "$src" ]] || return 1
    fi
}
firmware_invalidate_output()
{
    if [[ $prepare_only -eq 0 ]]; then
        rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming" \
            "$out/firmware-receipt.json" || echo 'cannot invalidate failed firmware output' >&2
    fi
}
firmware_cleanup()
{
    firmware_status=$?
    trap - EXIT
    if [[ $firmware_initializing -eq 1 ]]; then
        if firmware_original_lease; then
            rm -f "$firmware_lock/source" "$firmware_lock/owner" && rmdir "$firmware_lock" ||
                echo 'cannot release initial firmware cache lease' >&2
        else
            echo 'initial firmware cache lease changed; preserve it for inspection' >&2
        fi
        exit "$firmware_status"
    fi
    if [[ -n "$firmware_empty_root" ]] && ! rmdir "$firmware_empty_root"; then
        [[ $firmware_status -ne 0 ]] || firmware_status=1
    fi
    if ! firmware_owns_lease; then
        echo 'firmware cache lease changed; preserve foreign outputs and lease' >&2
        [[ $firmware_status -ne 0 ]] || firmware_status=1
        exit "$firmware_status"
    fi
    if [[ -n "$firmware_source_work" ]]; then
        if [[ $firmware_status -eq 0 && ! -e "$CACHE/.ember-firmware-pending.json" ]]; then
            if ! rm -rf -- "$firmware_source_work"; then
                echo "cannot remove own source evidence: $firmware_source_work" >&2
                firmware_status=1
            fi
        else
            echo "preserved firmware source evidence: $firmware_source_work" >&2
        fi
    fi
    # Invalidation must finish while the lease excludes another publisher.
    [[ $firmware_status -eq 0 ]] || firmware_invalidate_output
    if ! rm -f "$firmware_lock/source" "$firmware_lock/owner" || \
        ! rmdir "$firmware_lock"; then
        echo 'cannot release firmware cache lease; preserve it for inspection' >&2
        [[ $firmware_status -ne 0 ]] || firmware_status=1
        if firmware_original_lease; then
            firmware_invalidate_output
        else
            echo 'original lease was lost; preserve potentially foreign outputs' >&2
        fi
    fi
    # No shared output writes are permitted after a successful unlock.
    exit "$firmware_status"
}
trap firmware_cleanup EXIT
printf '%s\n' "$$" > "$firmware_lock/owner"
printf '%s\n' "$src" > "$firmware_lock/source"
firmware_initializing=0
mkdir -p "$out"
if [[ $prepare_only -eq 0 ]]; then
    # Invalidate this variant before fallible tool, source and build checks.
    rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming" \
        "$out/firmware-receipt.json"
    source "$ROOT/ember/firmware/gcc-profile.sh"
fi

# Use native host tools on EmberBSD or macOS. The firmware target is
# freestanding AArch64; an application sysroot does not prove compatibility.
if [[ $prepare_only -eq 1 ]]; then
    # Source preparation uses Git, Ruby and patch, with no target compiler.
    for tool in git ruby patch; do
        command -v "$tool" >/dev/null 2>&1 || { echo "missing host tool: $tool" >&2; exit 1; }
    done
else
# The SDK selector has supplied the bounded native PATH and compiler bindings.
: "${RPI_BUILD_MAKE:?native GNU make was not selected}"
target_family=${EMBER_TARGET_COMPILER_FAMILY:-clang}
firmware_empty_root=$(mktemp -d "$out/firmware-empty.XXXXXXXX")
case "$target_family" in
    clang)
        compiler_path=${EMBER_FIRMWARE_CC:?target Clang was not selected}
        compiler_version=$(firmware_gcc_query "$compiler_path" --version | sed -n '1s/.*clang version \([0-9][0-9.]*\).*/\1/p')
        compiler_machine=$(firmware_gcc_query "$compiler_path" --target=aarch64-unknown-none-elf -dumpmachine)
        compiler_identity=$(firmware_gcc_query "$compiler_path" --version)
        [[ "$compiler_machine" == aarch64-unknown-none-elf && -n "$compiler_version" ]] || {
            echo 'selected Clang does not match freestanding AArch64' >&2; exit 1;
        }
        firmware_gcc_include=${EMBER_FIRMWARE_INCLUDE:?target resource headers were not selected}
        firmware_specs=
        firmware_profile="$ROOT/ember/tools/firmware-toolchain.py"
        export CROSS_COMPILE= RPI_TFA_AR=${EMBER_FIRMWARE_AR:?} RPI_TOOLCHAIN_TAG=CLANGDWARF
        export CLANGDWARF_BIN="$EMBER_TARGET_PREFIX/bin/"
        firmware_tfa_flags=" CC=$compiler_path CPP=$compiler_path AS=$compiler_path LD=$compiler_path"
        firmware_tfa_flags+=" OC=${EMBER_FIRMWARE_OC:?} OD=${EMBER_FIRMWARE_OD:?} DTC=$(command -v dtc)"
        firmware_tfa_flags+=" target-arch-aarch64-llvm-clang=aarch64-unknown-none-elf"
        for profile_flag in "--target=aarch64-unknown-none-elf" "--sysroot=$firmware_empty_root" -ffreestanding -nostdinc "-isystem$firmware_gcc_include"; do
            firmware_tfa_flags+=" CFLAGS+=$profile_flag"
        done
        for profile_flag in "--target=aarch64-unknown-none-elf" "--sysroot=$firmware_empty_root" "--ld-path=${EMBER_FIRMWARE_LLD:?}" -nostdlib -Wl,-nostdlib; do
            firmware_tfa_flags+=" LDFLAGS+=$profile_flag"
        done
        firmware_edk2_flags="-D EMBER_CLANG_FIRMWARE_ROOT=$firmware_empty_root -D EMBER_CLANG_INCLUDE=$firmware_gcc_include -D EMBER_FIRMWARE_LLD=$EMBER_FIRMWARE_LLD"
        ;;
    gcc)
        : "${CROSS_COMPILE:?Set CROSS_COMPILE to an explicit GCC prefix inside EMBER_TARGET_PREFIX}"
        case "$CROSS_COMPILE" in "$EMBER_TARGET_PREFIX"/bin/*) ;; *) echo 'GCC prefix must be inside the target prefix' >&2; exit 1 ;; esac
        compiler_path="${CROSS_COMPILE}gcc"
        [[ -x "$compiler_path" ]] || { echo 'selected target GCC is missing' >&2; exit 1; }
        target_real=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$EMBER_TARGET_PREFIX")
        for target_tool in "$compiler_path" "${CROSS_COMPILE}gcc-ar" \
            "${CROSS_COMPILE}as" "${CROSS_COMPILE}ld" \
            "${CROSS_COMPILE}objcopy" "${CROSS_COMPILE}objdump"; do
            resolved_tool=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$target_tool")
            [[ -x "$resolved_tool" && "$resolved_tool" == "$target_real"/* ]] || {
                echo 'GCC tool escapes target prefix or is missing' >&2; exit 1;
            }
        done
        for gcc_component in as ld; do
            component=$(firmware_gcc_query "$compiler_path" "-print-prog-name=$gcc_component")
            component=$(command -v "$component") || {
                echo "GCC $gcc_component component is missing" >&2; exit 1;
            }
            component=$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$component")
            [[ -x "$component" && "$component" == "$target_real"/* ]] || {
                echo "GCC $gcc_component component escapes target prefix" >&2; exit 1;
            }
        done
        compiler_version=$(firmware_gcc_query "$compiler_path" -dumpfullversion)
        compiler_machine=$(firmware_gcc_query "$compiler_path" -dumpmachine)
        compiler_identity=$(firmware_gcc_query "$compiler_path" --version)
        compiler_macros=$(firmware_gcc_query "$compiler_path" -dM -E -x c /dev/null)
        if [[ ! "$compiler_version" =~ ^16(\.[0-9]+)*$ ]] || [[ "$compiler_machine" != aarch64-* ]] ||
            ! grep -qx '#define __GNUC__ 16' <<< "$compiler_macros" ||
            ! grep -qx '#define __aarch64__ 1' <<< "$compiler_macros" ||
            grep -q '^#define __clang__ ' <<< "$compiler_macros"; then
            echo 'explicit firmware GCC profile requires AArch64 GCC 16' >&2; exit 1
        fi
        firmware_specs="$ROOT/ember/firmware/gcc.specs"
        firmware_profile="$ROOT/ember/firmware/gcc-profile.sh"
        firmware_gcc_profile "$compiler_path" "$compiler_machine" "$compiler_version" "$firmware_empty_root" "$firmware_specs"
        export RPI_TFA_AR="${CROSS_COMPILE}gcc-ar" RPI_TOOLCHAIN_TAG=GCC
        firmware_tfa_flags=" CC=$compiler_path LD=$compiler_path"
        for profile_flag in "${firmware_gcc_cpp[@]}"; do firmware_tfa_flags+=" CFLAGS+=$profile_flag"; done
        for profile_flag in "${firmware_gcc_link[@]}"; do firmware_tfa_flags+=" LDFLAGS+=$profile_flag"; done
        firmware_edk2_flags="-D EMBER_FIRMWARE_ROOT=$firmware_empty_root -D EMBER_GCC_INCLUDE=$firmware_gcc_include -D EMBER_FIRMWARE_SPECS=$firmware_specs"
        ;;
    *) echo 'unknown target compiler family' >&2; exit 1 ;;
esac
compiler_sha256=$(shasum -a 256 "$compiler_path" | awk '{print $1}')
if [[ -z "$variant" ]]; then
    variant_flags+=(--tfa-flags "$firmware_tfa_flags" --edk2-flags "$firmware_edk2_flags")
else
    variant_flags[3]="${variant_flags[3]}$firmware_tfa_flags"
    variant_flags[5]="${variant_flags[5]} $firmware_edk2_flags"
fi
# Identity checks do not prove full TF-A/EDK2 freestanding compatibility.
fi

# The firmware series must name every diff exactly once.
series="$ROOT/ember/firmware/series"
[[ "$(sort "$series")" == "$(cd "$ROOT/ember/firmware" && ls ./*.diff | sed 's|^\./||' | sort)" ]] \
    || { echo 'firmware series must name every diff once' >&2; exit 1; }

# Only a newly created checkout may be pinned or populate its submodules.
# Existing shared source is checked and updated only at known patch targets.
if [[ ! -e "$src" && ! -L "$src" ]]; then
    git clone -q "$repo" "$src"
    git -C "$src" checkout -q "$revision"
    git -C "$src" submodule update -q --init --depth 1
    git -C "$src" submodule -q foreach \
        'git submodule update --init --depth 1 --recursive'
fi
[[ -d "$src/.git" && ! -L "$src" ]] || {
    echo 'firmware source must be the pinned Git checkout' >&2; exit 1;
}
firmware_source_work=$(mktemp -d "$out/source-state.XXXXXXXX")
if [[ $prepare_only -eq 0 ]]; then
    # Record actual complete flag strings. Normalize only verified empty-root
    # and specs roles when comparing the saved option profile.
    ruby -rjson -rdigest -e '
        path,variant,revision,family,compiler,version,machine,sha,prefix,include_dir,empty,specs,profile,tfa,edk=ARGV
        identity={"path"=>compiler,"version"=>version,"machine"=>machine,"sha256"=>sha}
        identity[family=="clang" ? "family" : "cross_compile"]=family=="clang" ? family : prefix
        inputs={"include"=>include_dir,"empty_root"=>empty,
          "profile_sha256"=>Digest::SHA256.file(profile).hexdigest,"tfa_flags"=>tfa,"edk2_flags"=>edk}
        inputs["specs_sha256"]=Digest::SHA256.file(specs).hexdigest unless family=="clang"
        File.write(path,JSON.pretty_generate({"variant"=>variant,"revision"=>revision,
          "compiler"=>identity,"freestanding"=>inputs})+"\n")
    ' "$firmware_source_work/build-options.json" "${variant#-}" "$revision" "$target_family" \
        "$compiler_path" "$compiler_version" "$compiler_machine" "$compiler_sha256" "$CROSS_COMPILE" \
        "$firmware_gcc_include" "$firmware_empty_root" "$firmware_specs" \
        "$firmware_profile" "${variant_flags[3]}" "${variant_flags[5]}"
fi
if [[ $prepare_only -eq 1 ]]; then
    ruby "$ROOT/ember/tools/firmware-source.rb" "$src" \
        "$firmware_source_work/update" --parent-lock "$$" --apply \
        > "$out/source-check.log" 2>&1
else
    ruby "$ROOT/ember/tools/firmware-source.rb" "$src" \
        "$firmware_source_work/update" --parent-lock "$$" --apply \
        --build-profile "$firmware_source_work/build-options.json" \
        > "$out/source-check.log" 2>&1
fi
if [[ $prepare_only -eq 1 ]]; then
    echo "prepared: $src"
    exit 0
fi
firmware_source_sha256=$(shasum -a 256 "$CACHE/.ember-firmware-source.json" | awk '{print $1}')

# Use host defaults and the selected CROSS_COMPILE, not implicit caller flags.
( cd "$src" && ./build.sh "${variant_flags[@]}" ) > "$out/build.log" 2>&1
# The upstream SD patch must have been restored. No unknown source change
# or changed manifest may be accepted as a successful firmware build.
ruby "$ROOT/ember/tools/firmware-source.rb" "$src" \
    "$firmware_source_work/postcheck" --parent-lock "$$" --verify-state \
    >> "$out/source-check.log" 2>&1
[[ $(shasum -a 256 "$CACHE/.ember-firmware-source.json" | awk '{print $1}') == "$firmware_source_sha256" ]] || {
    echo 'firmware source manifest changed during build' >&2; exit 1;
}

compiler_final_sha256=$(shasum -a 256 "$compiler_path" | awk '{print $1}')
[[ "$compiler_final_sha256" == "$compiler_sha256" ]] || {
    echo 'target compiler changed during firmware build' >&2; exit 1;
}
cp "$src/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming"

# Receipt: pins, series hashes and the artifact identity.
python3 - "$out" "$variant" "$revision" "$ROOT" "$compiler_path" \
    "$compiler_version" "$compiler_machine" "$compiler_identity" \
    "$compiler_sha256" "$CROSS_COMPILE" "$firmware_gcc_include" \
    "$firmware_empty_root" "$firmware_specs" "$firmware_tfa_flags" \
    "$firmware_edk2_flags" "$target_family" "$firmware_profile" << 'PYEOF'
import hashlib, json, os, sys
out, variant, revision, root, compiler, version, machine, identity, compiler_sha, prefix = sys.argv[1:11]
include, empty_root, specs, tfa_flags, edk2_flags, family, profile = sys.argv[11:18]
def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 16), b''):
            h.update(block)
    return h.hexdigest()
series = open(os.path.join(root, 'ember/firmware/series')).read().split()
receipt = {
    'kind': 'firmware',
    'revision': revision,
    'variant': variant.lstrip('-'),
    'compiler': {'path': compiler, 'version': version, 'machine': machine,
                 'identity': identity, 'sha256': compiler_sha,
                 'family': family, 'cross_compile': prefix},
    'freestanding': {'include': include, 'empty_root': empty_root,
                     'profile_sha256': sha(profile),
                     'tfa_flags': tfa_flags, 'edk2_flags': edk2_flags},
    'series': {d: sha(os.path.join(root, 'ember/firmware', d)) for d in series},
    'files': {'RPI_EFI.fd': sha(os.path.join(out, 'RPI_EFI.fd.incoming'))},
}
if family == 'gcc':
    receipt['freestanding']['specs_sha256'] = sha(specs)
with open(os.path.join(out, 'firmware-receipt.json'), 'w') as f:
    json.dump(receipt, f, indent=2, sort_keys=True)
    f.write('\n')
PYEOF
firmware_owns_lease || {
    echo 'firmware cache lease changed before publication; preserve foreign outputs' >&2
    exit 1
}
mv "$out/RPI_EFI.fd.incoming" "$out/RPI_EFI.fd"
echo "firmware: $out/RPI_EFI.fd"
