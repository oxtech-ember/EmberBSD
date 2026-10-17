#!/usr/bin/env bash
# Build the pinned eotics UEFI with this fork's ordered hardware diffs.
# Cross-build on the developer's machine with the host toolchain from brew.
# No containers, no per-run network package installs, no cloud steps.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 1 && $# -le 2 ]] || { echo 'usage: build-firmware.sh CACHE [--rp1-console|--rp1-trace]' >&2; exit 1; }
case "$1" in /*) CACHE="$1" ;; *) echo 'CACHE must be an absolute path' >&2; exit 1 ;; esac
revision=246e70acd939d62b5a79f895984afd0a93bd83e9
repo=https://github.com/eotics-com/rpi5-uefi.git
variant_flags=()
variant=
case "${2:-}" in
    '') ;;
    --rp1-console) variant=-rp1-console; variant_flags=(--tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE') ;;
    --rp1-trace)   variant=-rp1-trace;   variant_flags=(--tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE -D CM5_BOOT_TRACE=TRUE') ;;
    *) echo 'unknown firmware option' >&2; exit 1 ;;
esac
mkdir -p "$CACHE"
CACHE="$(cd "$CACHE" && pwd)"
out="$CACHE/firmware$variant"
src="$CACHE/rpi5-uefi-src"
mkdir -p "$out"

# Host tools. The canonical build host is EmberBSD itself with the
# tools from pkgsrc; a developer's macOS machine is supported for
# iteration. The firmware is a freestanding AArch64 target either way,
# so the NetBSD sysroot of the kernel build cannot serve this build.
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

# The firmware series must name every diff exactly once.
series="$ROOT/ember/firmware/series"
[[ "$(sort "$series")" == "$(cd "$ROOT/ember/firmware" && ls ./*.diff | sed 's|^\./||' | sort)" ]] \
    || { echo 'firmware series must name every diff once' >&2; exit 1; }

# A persistent source cache keeps the pinned revision and submodules.
# Re-runs reuse it; the working trees are restored to their pins first.
if [[ ! -d "$src/.git" ]]; then
    git clone -q "$repo" "$src"
fi
git -C "$src" fetch -q origin
git -C "$src" checkout -q "$revision"
git -C "$src" submodule update -q --init --depth 1
git -C "$src" submodule -q foreach 'git submodule update --init --depth 1 --recursive'
for tree in . edk2 edk2-platforms; do
    git -C "$src/$tree" reset -q --hard
    git -C "$src/$tree" clean -qfdx
done

# Apply the ordered fork diffs on the clean pinned checkout.
while IFS= read -r diff; do
    patch -s -p1 -F0 -d "$src/edk2-platforms" < "$ROOT/ember/firmware/$diff"
done < "$series"

# A failed rebuild must make the previous binary unusable as an image input.
rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming"
( cd "$src" && ./build.sh --model 5 "${variant_flags[@]}" ) > "$out/build.log" 2>&1
cp "$src/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming"
mv "$out/RPI_EFI.fd.incoming" "$out/RPI_EFI.fd"

# Receipt: pins, series hashes and the artifact identity.
python3 - "$out" "$variant" "$revision" "$ROOT" << 'PYEOF'
import hashlib, json, os, sys
out, variant, revision, root = sys.argv[1:5]
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
    'series': {d: sha(os.path.join(root, 'ember/firmware', d)) for d in series},
    'files': {'RPI_EFI.fd': sha(os.path.join(out, 'RPI_EFI.fd'))},
}
with open(os.path.join(out, 'firmware-receipt.json'), 'w') as f:
    json.dump(receipt, f, indent=2, sort_keys=True)
    f.write('\n')
PYEOF
echo "firmware: $out/RPI_EFI.fd"
