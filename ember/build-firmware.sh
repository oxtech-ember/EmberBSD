#!/usr/bin/env bash
# Build the pinned firmware with explicitly selected SDK tools.
set -euo pipefail
script_dir=${BASH_SOURCE[0]%/*}
[[ $script_dir != "${BASH_SOURCE[0]}" ]] || script_dir=.
ROOT="$(cd -- "$script_dir/.." && pwd -P)"
for arg in "$@"; do
    if [[ "$arg" == --prepare ]]; then
        exec "${BASH:-/bin/bash}" "$ROOT/ember/build-firmware-selected.sh" "$@"
    fi
done
: "${EMBERBSD_SDK_SOURCE:?Set EMBERBSD_SDK_SOURCE to the SDK source}"
: "${EMBER_HOST_PREFIX:?Set EMBER_HOST_PREFIX to the native SDK prefix}"
: "${EMBER_TARGET_PREFIX:?Set EMBER_TARGET_PREFIX to the target compiler prefix}"
exec "$EMBER_HOST_PREFIX/bin/python3.15" -B "$ROOT/ember/tools/firmware-toolchain.py" \
    --sdk-source "$EMBERBSD_SDK_SOURCE" --host-prefix "$EMBER_HOST_PREFIX" \
    --target-prefix "$EMBER_TARGET_PREFIX" \
    --host-family "${EMBER_HOST_COMPILER_FAMILY:-clang}" \
    --target-family "${EMBER_TARGET_COMPILER_FAMILY:-clang}" --run -- \
    "$ROOT/ember/build-firmware-selected.sh" "$@"
