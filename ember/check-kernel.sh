#!/bin/sh
# Run the kernel source contracts. build-kernel.sh does not run them.
# With BASE_COMMIT, run only the contracts that the changes since that
# commit can affect (see ember/tools/kernel-contracts-inputs.txt).
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
fail() { echo "$*" >&2; exit 1; }
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || fail 'usage: check-kernel.sh ABSOLUTE_OUTPUT [BASE_COMMIT]'
out=$1
base=${2:-}
case "$out" in /*) ;; *) fail 'output must be an absolute path' ;; esac
sdk_source=${EMBERBSD_SDK_SOURCE:-}
host_prefix=${EMBER_HOST_PREFIX:-}
host_family=${EMBER_HOST_COMPILER_FAMILY:-clang}
wpa_source=${EMBER_WPA_SOURCE:-}
case "$wpa_source" in ''|/*) ;; *) fail 'EMBER_WPA_SOURCE must be an absolute path';; esac
if [ -n "$wpa_source" ]; then
    [ -f "$wpa_source/src/drivers/driver_bsd.c" ] &&
        [ -f "$wpa_source/src/common/ieee802_11_defs.h" ] ||
        fail 'EMBER_WPA_SOURCE must point to the prepared Ports WPA tree'
fi
case "$sdk_source" in /*) ;; *) fail 'set EMBERBSD_SDK_SOURCE to the SDK source root';; esac
case "$host_prefix" in /*) ;; *) fail 'set EMBER_HOST_PREFIX to the selected native prefix';; esac
case "$host_family" in clang|gcc) ;; *) fail 'EMBER_HOST_COMPILER_FAMILY must be clang or gcc';; esac
native_check="$sdk_source/scripts/run-native-check.py"
[ -f "$native_check" ] || fail 'SDK source lacks the shared native-check runner'
python="$host_prefix/bin/python3.15"
[ -x "$python" ] || fail 'selected prefix lacks its Python 3.15'
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd -P)
case "$out/" in //|"$src/"*) fail 'output must be outside the source tree' ;; esac
# The kernel build uses the same compiler bindings. Do not run both at once.
mkdir "$out/.build-lock" 2>/dev/null || fail 'output is already in use (or has a stale .build-lock)'
trap 'rmdir "$out/.build-lock"' EXIT
# mksh can skip the EXIT trap after a signal trap. Clean up here.
trap 'rmdir "$out/.build-lock"; trap - EXIT; exit 1' HUP INT TERM
selection=
rm -f "$out/contract-changes" "$out/contract-selection"
if [ -n "$base" ]; then
    selection="$out/contract-selection"
    git_top=$(git -C "$src" rev-parse --show-toplevel) || git_top=
    [ "$git_top" = "$src" ] || fail 'BASE_COMMIT needs the source tree as a Git checkout'
    git -C "$src" rev-parse --verify --quiet "$base^{commit}" > /dev/null ||
        fail "unknown base commit: $base"
    # Tracked changes since BASE_COMMIT, and new files that Git does not ignore.
    # A rename gives both paths. Paths are relative to the source root.
    { git -C "$src" -c core.quotePath=false diff --no-renames --name-only "$base" -- &&
        git -C "$src" -c core.quotePath=false ls-files --others --exclude-standard; } \
        > "$out/contract-changes" || fail "cannot list the changes since $base"
    "$python" "$src/ember/tools/kernel-contracts-select.py" "$src" \
        "$out/contract-changes" "$selection"
fi
# EMBER_CONTRACT_JOBS selects how many contracts run at the same time.
# With more than one, a failed contract does not stop the others, and each
# contract gets no standard input.
contract_jobs=${EMBER_CONTRACT_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}
case $contract_jobs in ''|0|*[!0-9]*) fail 'EMBER_CONTRACT_JOBS must be a positive number';; esac
contracts=all
if [ "$(uname -s)" != NetBSD ]; then
    contracts=portable
fi
# Bindings of an earlier run can select other compiler flags.
rm -rf "$out/native-check-commands"
rm -f "$out/contracts.log"
# The common SDK runner binds native compiler and ABI arguments even for
# existing contracts that accept only an executable CC and ignore CFLAGS.
status=0
"$python" "$native_check" --host-prefix "$host_prefix" \
    --compiler-family "$host_family" --required-role ruby --required-role awk \
    --cppflag="-I$host_prefix/include" --ldflag="-L$host_prefix/lib" \
    --ldflag="-Wl,-rpath,$host_prefix/lib" \
    --tool sh --cwd "$src" --bindings "$out/native-check-commands" \
    --log "$out/contracts.log" -- \
    "$src/ember/tools/kernel-contracts.sh" "$src" "$contracts" \
    "${PVR_QUERY_FIRMWARE:-}" "$selection" "$contract_jobs" "$wpa_source" || status=$?
[ ! -f "$out/contracts.log" ] || cat "$out/contracts.log"
exit "$status"
