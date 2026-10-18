#!/usr/bin/env bash
# Build the pinned eotics UEFI with this fork's ordered hardware diffs.
# Cross-build on a development host with the common AArch64 GCC16 toolchain.
# No containers, no per-run network package installs, no cloud steps.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# --prepare pins the source cache and exits; it may appear anywhere.
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
mkdir -p "$CACHE"
CACHE="$(cd "$CACHE" && pwd)"
out="$CACHE/firmware$variant"
src="$CACHE/rpi5-uefi-src"
mkdir -p "$out"

if [[ $prepare_only -eq 0 ]]; then
    firmware_cleanup()
    {
        firmware_status=$?
        trap - EXIT
        if [[ $firmware_status -ne 0 ]]; then
            # Keep the original status and build.log if cleanup also fails.
            rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming" \
                "$out/firmware-receipt.json" || :
        fi
        exit "$firmware_status"
    }
    trap firmware_cleanup EXIT
    # Invalidate this variant before any fallible host, source or build check.
    rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming" \
        "$out/firmware-receipt.json"
fi

# Use native host tools on EmberBSD or macOS. The firmware target is
# freestanding AArch64; an application sysroot does not prove compatibility.
if [[ $prepare_only -eq 1 ]]; then
    # Preparing the cache needs only Git, on any host.
    command -v git >/dev/null 2>&1 || { echo 'missing host tool: git' >&2; exit 1; }
else
host_system=$(uname -s)
case "$host_system" in
    NetBSD)
        # EmberBSD host: pkgsrc provides the cross compiler and tools.
        PATH="/usr/pkg/bin:/usr/pkg/sbin:$PATH"
        export PATH
        export RPI_BUILD_MAKE=gmake
        ;;
    Darwin)
        # Developer machine: a pinned local toolchain directory wins;
        # the brew prefix is only the bootstrap fallback.
        case "$(uname -m)" in
            arm64) brewbin=/opt/homebrew/bin ;;
            *)     brewbin=/usr/local/bin ;;
        esac
        if [[ -n "${EMBER_AARCH64_ELF_TOOLCHAIN:-}" ]]; then
            case "$EMBER_AARCH64_ELF_TOOLCHAIN" in
                /*) ;;
                *) echo 'EMBER_AARCH64_ELF_TOOLCHAIN must be absolute' >&2; exit 1 ;;
            esac
            PATH="$EMBER_AARCH64_ELF_TOOLCHAIN/bin:$PATH"
        fi
        case ":$PATH:" in *":$brewbin:"*) ;; *) PATH="$brewbin:$PATH" ;; esac
        export PATH
        export RPI_BUILD_MAKE=gmake
        ;;
    *)
        echo "unsupported build host: $host_system" >&2
        exit 1
        ;;
esac
missing=0
# pkgsrc ships the bare-metal toolchain as aarch64-none-elf-;
# accept both it and the classic aarch64-elf- prefix.
for tool in gmake iasl dtc python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing host tool: $tool" >&2; missing=1; }
done
case "${CROSS_COMPILE:-}" in
    '') if command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
            export CROSS_COMPILE=aarch64-none-elf-
        elif command -v aarch64-elf-gcc >/dev/null 2>&1; then
            export CROSS_COMPILE=aarch64-elf-
        else
            echo 'missing host tool: aarch64-none-elf-gcc (pkgsrc cross/aarch64-none-elf-gcc) or aarch64-elf-gcc' >&2
            missing=1
        fi ;;
esac
python3 -c 'import elftools' >/dev/null 2>&1 || { echo 'missing python3 module: pyelftools' >&2; missing=1; }
[[ $missing -eq 0 ]] || exit 1

compiler_path=$(command -v "${CROSS_COMPILE}gcc") || {
    echo "missing target compiler: ${CROSS_COMPILE}gcc" >&2; exit 1;
}
case "$compiler_path" in /*) ;; *) compiler_path="$PWD/$compiler_path" ;; esac
compiler_version=$("$compiler_path" -dumpfullversion) || {
    echo 'cannot read target GCC version' >&2; exit 1;
}
compiler_machine=$("$compiler_path" -dumpmachine) || {
    echo 'cannot read target GCC machine' >&2; exit 1;
}
compiler_identity=$("$compiler_path" --version) || {
    echo 'cannot read target GCC identity' >&2; exit 1;
}
compiler_macros=$("$compiler_path" -dM -E -x c /dev/null) || {
    echo 'cannot read target GCC predefined macros' >&2; exit 1;
}
if [[ ! "$compiler_version" =~ ^16(\.[0-9]+)*$ ]] ||
    [[ "$compiler_machine" != aarch64-* ]] ||
    ! grep -qx '#define __GNUC__ 16' <<< "$compiler_macros" ||
    ! grep -qx '#define __aarch64__ 1' <<< "$compiler_macros" ||
    grep -q '^#define __clang__ ' <<< "$compiler_macros"; then
    echo 'firmware requires AArch64 GCC major 16; Clang is not a target compiler' >&2
    exit 1
fi
compiler_sha256=$(shasum -a 256 "$compiler_path" | awk '{print $1}')
# Identity checks do not prove full TF-A/EDK2 freestanding compatibility.
fi

# The firmware series must name every diff exactly once.
series="$ROOT/ember/firmware/series"
[[ "$(sort "$series")" == "$(cd "$ROOT/ember/firmware" && ls ./*.diff | sed 's|^\./||' | sort)" ]] \
    || { echo 'firmware series must name every diff once' >&2; exit 1; }

# A persistent source cache keeps the pinned revision and submodules.
# A cache prepared on a machine with git carries .ember-pinned with the
# revision; a build host without git (an EmberBSD board with the toolchain
# packages) trusts that marker instead of re-fetching. The series
# application is tracked by .ember-series with the combined series hash.
if [[ ! -d "$src/.git" ]]; then
    command -v git >/dev/null 2>&1 || {
        echo "no git and no source cache: prepare the cache on a host with git" >&2
        exit 1
    }
    git clone -q "$repo" "$src"
fi
if [[ -f "$src/.ember-pinned" && "$(cat "$src/.ember-pinned")" == "$revision" ]] \
    && ! command -v git >/dev/null 2>&1; then
    : # Prepared cache on a host without git: keep the transferred state.
else
    command -v git >/dev/null 2>&1 || {
        echo "cache is not pinned to $revision and git is unavailable" >&2
        exit 1
    }
    git -C "$src" fetch -q origin
    git -C "$src" checkout -q "$revision"
    git -C "$src" submodule update -q --init --depth 1
    git -C "$src" submodule -q foreach 'git submodule update --init --depth 1 --recursive'
    for tree in . edk2 edk2-platforms; do
        git -C "$src/$tree" reset -q --hard
        git -C "$src/$tree" clean -qfdx
    done
    # Nested submodule checkouts can hold files from a fetched tip that
    # conflict with the pinned commit; restore them too.
    git -C "$src" submodule -q foreach --recursive 'git reset -q --hard && git clean -qfdx'
    printf '%s\n' "$revision" > "$src/.ember-pinned"
    [[ $prepare_only -eq 0 ]] || { echo "prepared: $src"; exit 0; }
fi

# Apply the ordered fork diffs once; the marker makes re-runs idempotent.
series_identity=$( (cd "$ROOT/ember/firmware" && cat $(cat "$series") | shasum -a 256) | awk '{print $1}')
if [[ ! -f "$src/.ember-series" || "$(cat "$src/.ember-series")" != "$series_identity" ]]; then
    while IFS= read -r diff; do
        patch -s -p1 -F0 -d "$src/edk2-platforms" < "$ROOT/ember/firmware/$diff"
    done < "$series"
    printf '%s\n' "$series_identity" > "$src/.ember-series"
fi

( cd "$src" && ./build.sh "${variant_flags[@]}" ) > "$out/build.log" 2>&1
compiler_final_sha256=$(shasum -a 256 "$compiler_path" | awk '{print $1}')
[[ "$compiler_final_sha256" == "$compiler_sha256" ]] || {
    echo 'target compiler changed during firmware build' >&2; exit 1;
}
cp "$src/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming"

# Receipt: pins, series hashes and the artifact identity.
python3 - "$out" "$variant" "$revision" "$ROOT" "$compiler_path" \
    "$compiler_version" "$compiler_machine" "$compiler_identity" \
    "$compiler_sha256" "$CROSS_COMPILE" << 'PYEOF'
import hashlib, json, os, sys
out, variant, revision, root, compiler, version, machine, identity, compiler_sha, prefix = sys.argv[1:11]
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
                 'cross_compile': prefix},
    'series': {d: sha(os.path.join(root, 'ember/firmware', d)) for d in series},
    'files': {'RPI_EFI.fd': sha(os.path.join(out, 'RPI_EFI.fd.incoming'))},
}
with open(os.path.join(out, 'firmware-receipt.json'), 'w') as f:
    json.dump(receipt, f, indent=2, sort_keys=True)
    f.write('\n')
PYEOF
mv "$out/RPI_EFI.fd.incoming" "$out/RPI_EFI.fd"
echo "firmware: $out/RPI_EFI.fd"
