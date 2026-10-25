#!/bin/sh
# Existing source contracts: portable subset on the build host, all on NetBSD.
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || {
    echo 'usage: kernel-contracts.sh SOURCE_ROOT [all|portable]' >&2; exit 1;
}
src=$(CDPATH= cd -- "$1" && pwd)
mode=${2:-all}
case "$mode" in all|portable) ;; *) echo 'invalid contract mode' >&2; exit 1;; esac
python=${NETBSD2_PYTHON:-$(command -v python3 || true)}
[ -n "$python" ] && [ -x "$python" ] || {
    echo 'set NETBSD2_PYTHON to the host Python 3 executable' >&2; exit 1;
}
"$python" "$src/ember/tools/bluetooth-uart-contract.py" "$src/sys/dev/ic/bcm2712btcom.c"
"$python" "$src/ember/tools/bluetooth-rc-contract.py"
sh "$src/ember/tools/a733-accelerator-clock-contract.sh"
sh "$src/ember/tools/a733-power-contract.sh"
"$python" "$src/ember/tests/a733-npu-observe.py"
sh "$src/ember/tools/a733-display-observer-contract.sh"
sh "$src/ember/tools/a733-gpu-identification-contract.sh"
sh "$src/ember/tools/a733-gpu-fwinfo-contract.sh"
sh "$src/ember/tools/a733-gpu-fwimage-contract.sh"
sh "$src/ember/tools/a733-gpu-mips-contract.sh"
sh "$src/ember/tools/a733-gpu-fwabi-contract.sh"
sh "$src/ember/tools/a733-gpu-fwmem-contract.sh"
sh "$src/ember/tools/a733-gpu-fwinit-contract.sh"
sh "$src/ember/tools/a733-gpu-fwstart-contract.sh"
sh "$src/ember/tools/a733-gpu-fwboot-contract.sh"
sh "$src/ember/tools/a733-gpu-fwprobe-contract.sh"
sh "$src/ember/tools/a733-gpu-fwcompute-contract.sh"
sh "$src/ember/tools/a733-gpu-fwcompute-vm-contract.sh"
sh "$src/ember/tools/a733-gpu-fwheap-observe-contract.sh"
sh "$src/ember/tools/pvr-freelist-contract.sh"
sh "$src/ember/tools/pvr-exec-hwrt-contract.sh"
sh "$src/ember/tools/a733-gpu-fwbridge-contract.sh"
sh "$src/ember/tools/a733-gpu-fwservice-contract.sh"
sh "$src/ember/tools/a733-gpu-fwfile-contract.sh"
sh "$src/ember/tools/a733-gpu-mmu-contract.sh"
sh "$src/ember/tools/a733-gpu-vm-contract.sh"
sh "$src/ember/tools/pvr-render-contract.sh" "$src"
sh "$src/ember/tools/pvr-heap-contract.sh" "$src"
sh "$src/ember/tools/pvr-bo-contract.sh" "$src"
sh "$src/ember/tools/pvr-vm-contract.sh" "$src"
sh "$src/ember/tools/pvr-file-contract.sh" "$src"
sh "$src/ember/tools/pvr-stream-contract.sh" "$src"
sh "$src/ember/tools/pvr-context-contract.sh" "$src"
sh "$src/ember/tools/pvr-hwrt-contract.sh" "$src"
sh "$src/ember/tools/pvr-submit-contract.sh" "$src"
sh "$src/ember/tools/pvr-render-attach-contract.sh" "$src"
sh "$src/ember/tools/pvr-exec-contract.sh"
if [ -n "${PVR_QUERY_FIRMWARE:-}" ]; then
    sh "$src/ember/tools/pvr-query-contract.sh" "$src"
else
    echo 'PowerVR metadata query contract pending: set PVR_QUERY_FIRMWARE to the pinned firmware.'
fi
sh "$src/ember/tools/bcm2712-display-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-display-irq-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-display-hsm-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-display-dvp-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-takeover-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-dma-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-queue-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-irq-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-fault-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-uapi-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-bo-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-sg-contract.sh" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcm2712-v3d-bo-stats-contract.rb"
"$python" "$src/ember/tools/drm-dmabuf-contract.py" map
"$python" "$src/ember/tools/drm-dmabuf-contract.py" lifetime
"$python" "$src/ember/tools/drm-dmabuf-contract.py" array
"$python" "$src/ember/tools/drm-dmabuf-contract.py" sync
"${RUBY:-ruby}" "$src/ember/tools/bcm2712-v3d-prime-contract.rb"
"${RUBY:-ruby}" "$src/ember/tools/drm-gem-fb-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-resv-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-read-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-vblank-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-vblank-native-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-flip-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-scanout-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bcmv3d-job-resv-contract.rb" "$src"
sh "$src/ember/tools/bcm2712-v3d-job-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-csd-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-cl-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-wait-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-sync-contract.sh" "$src"
sh "$src/ember/tools/drm-syncobj-wait-contract.sh" "$src"
sh "$src/ember/tools/drm-syncobj-dispatch-contract.sh" "$src"
sh "$src/ember/tools/drm-syncobj-find-contract.sh" "$src"
sh "$src/ember/tools/dma-fence-chain-contract.sh" "$src"
sh "$src/ember/tools/linux-idr-contract.sh" "$src"
sh "$src/ember/tools/drm-jiffies-contract.sh" "$src"
sh "$src/ember/tools/i915-elsq-desc-contract.sh" "$src"
"$python" "$src/ember/tests/amdgpu-reset-contract.py" "$src"
"$python" "$src/ember/tests/amdgpu-stable-pstate-contract.py" "$src"
"$python" "$src/ember/tests/amdgpu-mem-sync-contract.py" "$src"
sh "$src/ember/tools/i915-rps-lifetime-contract.sh" "$src"
"$python" "$src/ember/tools/i915-engine-uabi-contract.py" "$src"
sh "$src/ember/tools/drm-pci-identity-contract.sh" "$src"
sh "$src/ember/tools/linux-rcu-replace-contract.sh" "$src"
"${RUBY:-ruby}" "$src/ember/tools/simplefb-contract.rb"
"${RUBY:-ruby}" "$src/ember/tools/serial-console-ready-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/bootfb-serial-handoff-contract.rb" "$src"
"${RUBY:-ruby}" "$src/ember/tools/drm-hotplug-contract.rb"
sh "$src/ember/tests/cemac-stop.sh" "$src"
sh "$src/ember/tests/bwfm-link-events.sh" "$src"
sh "$src/ember/tests/bwfm-link-check.sh" "$src"
sh "$src/ember/tests/bwfm-sta.sh" "$src"
sh "$src/ember/tests/aicwf-sta.sh"
sh "$src/ember/tests/aicwf-vht.sh"
sh "$src/ember/tests/bwfm-scan-padding.sh" "$src"
sh "$src/ember/tests/bwfm-key-epoch.sh" "$src"
if [ "$mode" = all ]; then
    "$python" "$src/ember/tools/btuart-contract.py" "$src/sys/dev/bluetooth/btuart.c"
    "$python" "$src/ember/tools/bluetooth-control-contract.py" "$src/ember/tools/bluetooth-control.c"
    "$python" "$src/ember/tools/bluetooth-pair-contract.py" "$src/ember/tools/bluetooth-pair.c"
    "$python" "$src/ember/tools/codec-contract.py" "$src"
else
    echo 'Native contracts pending: run kernel-contracts.sh SOURCE_ROOT all on NetBSD.'
fi
