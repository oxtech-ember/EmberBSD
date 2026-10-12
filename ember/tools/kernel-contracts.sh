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
sh "$src/ember/tools/a733-gpu-identification-contract.sh"
sh "$src/ember/tools/a733-gpu-fwinfo-contract.sh"
sh "$src/ember/tools/a733-gpu-fwimage-contract.sh"
sh "$src/ember/tools/a733-gpu-mips-contract.sh"
sh "$src/ember/tools/a733-gpu-fwabi-contract.sh"
sh "$src/ember/tools/a733-gpu-fwmem-contract.sh"
sh "$src/ember/tools/a733-gpu-fwinit-contract.sh"
sh "$src/ember/tools/a733-gpu-fwstart-contract.sh"
sh "$src/ember/tools/a733-gpu-fwboot-contract.sh"
sh "$src/ember/tools/bcm2712-v3d-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-takeover-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-dma-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-queue-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-irq-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-fault-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-uapi-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-bo-contract.sh" "$src"
"${RUBY:-ruby}" "$src/ember/tools/drm-prime-map-contract.rb"
"${RUBY:-ruby}" "$src/ember/tools/drm-dmabuf-lifetime-contract.rb"
"${RUBY:-ruby}" "$src/ember/tools/bcm2712-v3d-prime-contract.rb"
sh "$src/ember/tools/bcm2712-v3d-job-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-csd-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-cl-contract.sh" "$src"
sh "$src/ember/tools/bcm2712-v3d-sync-contract.sh" "$src"
sh "$src/ember/tools/drm-syncobj-wait-contract.sh" "$src"
sh "$src/ember/tools/drm-syncobj-dispatch-contract.sh" "$src"
sh "$src/ember/tools/drm-jiffies-contract.sh" "$src"
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
