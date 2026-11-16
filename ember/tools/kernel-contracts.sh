#!/bin/sh
# Existing source contracts: portable subset on the build host, all on NetBSD.
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 4 ] || {
    echo 'usage: kernel-contracts.sh SOURCE_ROOT [all|portable] [PVR_FIRMWARE] [SELECTION]' >&2; exit 1;
}
src=$(CDPATH= cd -- "$1" && pwd)
mode=${2:-all}
if [ "$#" -ge 3 ]; then PVR_QUERY_FIRMWARE=$3; export PVR_QUERY_FIRMWARE; fi
# SELECTION is a file from kernel-contracts-select.py. Without it, run all.
selection=${4:-}
if [ -n "$selection" ] && [ ! -f "$selection" ]; then
    echo 'contract selection file missing' >&2; exit 1
fi
run() {
    if [ -n "$selection" ]; then
        script=
        for argument; do
            case $argument in "$src"/ember/*) script=${argument#"$src"/}; break;; esac
        done
        if [ -n "$script" ]; then
            status=0
            grep -Fqx -- "$script" "$selection" || status=$?
            [ "$status" -ne 1 ] || return 0
            [ "$status" -eq 0 ] || { echo 'cannot read the contract selection' >&2; exit 1; }
        fi
    fi
    "$@"
}
case "$mode" in all|portable) ;; *) echo 'invalid contract mode' >&2; exit 1;; esac
python=${NETBSD2_PYTHON:-$(command -v python3 || true)}
[ -n "$python" ] && [ -x "$python" ] || {
    echo 'set NETBSD2_PYTHON to the host Python 3 executable' >&2; exit 1;
}
run "$python" "$src/ember/tests/efi-runtime-copy-contract.py" "$src"
run "$python" "$src/ember/tests/amdgpio-optional-aei-contract.py" "$src"
run "$python" "$src/ember/tools/bluetooth-uart-contract.py" "$src/sys/dev/ic/bcm2712btcom.c"
run "$python" "$src/ember/tools/bluetooth-rc-contract.py"
run sh "$src/ember/tools/a733-accelerator-clock-contract.sh"
run sh "$src/ember/tools/a733-power-contract.sh"
run "$python" "$src/ember/tests/a733-npu-observe.py"
run sh "$src/ember/tools/a733-display-observer-contract.sh"
run sh "$src/ember/tools/a733-gpu-identification-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwinfo-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwimage-contract.sh"
run sh "$src/ember/tools/a733-gpu-mips-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwabi-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwmem-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwinit-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwstart-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwboot-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwprobe-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwcompute-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwcompute-vm-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwheap-observe-contract.sh"
run sh "$src/ember/tools/pvr-freelist-contract.sh"
run sh "$src/ember/tools/pvr-exec-hwrt-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwbridge-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwservice-contract.sh"
run sh "$src/ember/tools/a733-gpu-fwfile-contract.sh"
run sh "$src/ember/tools/a733-gpu-mmu-contract.sh"
run sh "$src/ember/tools/a733-gpu-vm-contract.sh"
run sh "$src/ember/tools/pvr-render-contract.sh" "$src"
run sh "$src/ember/tools/pvr-heap-contract.sh" "$src"
run sh "$src/ember/tools/pvr-bo-contract.sh" "$src"
run sh "$src/ember/tools/pvr-vm-contract.sh" "$src"
run sh "$src/ember/tools/pvr-file-contract.sh" "$src"
run sh "$src/ember/tools/pvr-stream-contract.sh" "$src"
run sh "$src/ember/tools/pvr-context-contract.sh" "$src"
run sh "$src/ember/tools/pvr-hwrt-contract.sh" "$src"
run sh "$src/ember/tools/drm-scheduler-contract.sh" "$src"
run sh "$src/ember/tools/pvr-submit-contract.sh" "$src"
run sh "$src/ember/tools/pvr-render-attach-contract.sh" "$src"
run sh "$src/ember/tools/pvr-exec-contract.sh"
if [ -n "${PVR_QUERY_FIRMWARE:-}" ]; then
    run sh "$src/ember/tools/pvr-query-contract.sh" "$src"
else
    echo 'PowerVR metadata query contract pending: set PVR_QUERY_FIRMWARE to the pinned firmware.'
fi
run sh "$src/ember/tools/bcm2712-display-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-display-irq-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-display-hsm-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-display-dvp-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-takeover-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-dma-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-queue-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-irq-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-fault-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-uapi-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-bo-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-sg-contract.sh" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcm2712-v3d-bo-stats-contract.rb"
run "$python" "$src/ember/tools/drm-dmabuf-contract.py" map
run "$python" "$src/ember/tools/drm-dmabuf-contract.py" lifetime
run "$python" "$src/ember/tools/drm-dmabuf-contract.py" array
run "$python" "$src/ember/tools/drm-dmabuf-contract.py" sync
run "${RUBY:-ruby}" "$src/ember/tools/bcm2712-v3d-prime-contract.rb"
run "${RUBY:-ruby}" "$src/ember/tools/drm-gem-fb-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-resv-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-read-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-vblank-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-vblank-native-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-flip-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-scanout-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bcmv3d-job-resv-contract.rb" "$src"
run sh "$src/ember/tools/bcm2712-v3d-job-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-csd-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-cl-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-wait-contract.sh" "$src"
run sh "$src/ember/tools/bcm2712-v3d-sync-contract.sh" "$src"
run sh "$src/ember/tools/drm-syncobj-wait-contract.sh" "$src"
run sh "$src/ember/tools/drm-syncobj-dispatch-contract.sh" "$src"
run sh "$src/ember/tools/drm-syncobj-find-contract.sh" "$src"
run sh "$src/ember/tools/drm-syncobj-flatten-contract.sh" "$src"
run sh "$src/ember/tools/dma-fence-chain-contract.sh" "$src"
run sh "$src/ember/tools/linux-idr-contract.sh" "$src"
run sh "$src/ember/tools/ww-mutex-contract.sh" "$src"
run sh "$src/ember/tools/drm-jiffies-contract.sh" "$src"
run sh "$src/ember/tools/i915-elsq-desc-contract.sh" "$src"
run "$python" "$src/ember/tests/amdgpu-reset-contract.py" "$src"
run "$python" "$src/ember/tests/amdgpu-stable-pstate-contract.py" "$src"
run "$python" "$src/ember/tests/amdgpu-mem-sync-contract.py" "$src"
run "$python" "$src/ember/tests/amdgpu-display-format-contract.py" "$src"
run "$python" "$src/ember/tests/amdgpu-discovery-guard-contract.py" "$src"
run sh "$src/ember/tools/i915-rps-lifetime-contract.sh" "$src"
run "$python" "$src/ember/tools/i915-engine-uabi-contract.py" "$src"
run sh "$src/ember/tools/drm-pci-identity-contract.sh" "$src"
run sh "$src/ember/tools/linux-rcu-replace-contract.sh" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/simplefb-contract.rb"
run "${RUBY:-ruby}" "$src/ember/tools/late-splash-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/serial-console-ready-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/bootfb-serial-handoff-contract.rb" "$src"
run "${RUBY:-ruby}" "$src/ember/tools/drm-hotplug-contract.rb"
run sh "$src/ember/tools/cpu-acpi-capacity-contract.sh"
run sh "$src/ember/tests/cemac-stop.sh" "$src"
run sh "$src/ember/tests/bwfm-link-events.sh" "$src"
run sh "$src/ember/tests/bwfm-link-check.sh" "$src"
run sh "$src/ember/tests/bwfm-sta.sh" "$src"
run sh "$src/ember/tests/aicwf-sta.sh"
run sh "$src/ember/tests/aicwf-vht.sh"
run sh "$src/ember/tests/bwfm-scan-padding.sh" "$src"
run sh "$src/ember/tests/bwfm-key-epoch.sh" "$src"
run sh "$src/ember/tests/drm2-linux-compat.sh"
run "$python" "$src/ember/tools/efidrm-commit-tail-contract.py"
if [ "$mode" = all ]; then
    run "$python" "$src/ember/tools/btuart-contract.py" "$src/sys/dev/bluetooth/btuart.c"
    run "$python" "$src/ember/tools/bluetooth-control-contract.py" "$src/ember/tools/bluetooth-control.c"
    run "$python" "$src/ember/tools/bluetooth-pair-contract.py" "$src/ember/tools/bluetooth-pair.c"
    run "$python" "$src/ember/tools/codec-contract.py" "$src"
else
    echo 'Native contracts pending: run kernel-contracts.sh SOURCE_ROOT all on NetBSD.'
fi
