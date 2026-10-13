#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted). Test recovery and chainloading in isolated QEMU.
set -eu
[ "$#" -eq 4 ] || { echo "Usage: $0 OS_SOURCE NEW_WORK CROSS_PREFIX SYSROOT" >&2; exit 2; }
: "${QEMU:?Select the native qemu-system-aarch64}"
: "${EFI_CODE:?Select its raw AArch64 UEFI code image}"
: "${EFI_VARS:?Select its blank raw UEFI variables image}"
owner=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$2"
work=$(CDPATH= cd -- "$2" && pwd)
sh "$owner/build.sh" "$1" "$work/probe" "$3" "$4"
sh "$owner/build.sh" "$1" "$work/chain" "$3" "$4" --test-chain
qpid=
watcher=
cleanup() {
    [ -z "$qpid" ] || kill -TERM "$qpid" 2>/dev/null || :
    [ -z "$watcher" ] || kill -TERM "$watcher" 2>/dev/null || :
}
trap cleanup EXIT HUP INT TERM
for mode in gop no-gop journal-error; do
    run=$work/$mode
    mkdir "$run"
    mformat -i "$run/fat.img" -C -T 131072 ::
    mmd -i "$run/fat.img" ::/EFI ::/EFI/BOOT
    mcopy -i "$run/fat.img" "$work/probe/BOOTAA64.EFI" ::/EFI/BOOT/BOOTAA64.EFI
    mcopy -i "$run/fat.img" "$work/chain/BOOTAA64.EFI" ::/EFI/BOOT/ember-hdmi-original.efi
    if [ "$mode" = journal-error ]; then
        mmd -i "$run/fat.img" ::/ember-hdmi-probe.log
    fi
    cp "$EFI_VARS" "$run/vars.fd"
    set --
    [ "$mode" = no-gop ] || set -- -device virtio-gpu-pci
    "$QEMU" -machine virt -accel tcg -cpu cortex-a72 -m 512 -nodefaults \
        -display none -serial stdio -monitor none "$@" \
        -drive "if=pflash,format=raw,readonly=on,file=$EFI_CODE" \
        -drive "if=pflash,format=raw,file=$run/vars.fd" \
        -device virtio-blk-pci,drive=fat \
        -drive "if=none,id=fat,format=raw,file=$run/fat.img" > "$run/qemu.log" 2>&1 &
    qpid=$!
    (sleep 55; touch "$run/timed-out"; kill -TERM "$qpid" 2>/dev/null || :) &
    watcher=$!
    wait "$qpid"
    qpid=
    kill -TERM "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    watcher=
    [ ! -e "$run/timed-out" ]
    mcopy -i "$run/fat.img" ::/chain-pass.txt "$run/chain.log"
    mcopy -i "$run/fat.img" ::/EFI/BOOT/BOOTAA64.EFI "$run/restored.efi"
    cmp "$work/chain/BOOTAA64.EFI" "$run/restored.efi"
    grep -q 'PASS: chain loaded' "$run/chain.log"
    if [ "$mode" = journal-error ]; then
        printf 'PASS: journal failure still restores and chainloads the original loader\n'
        continue
    fi
    mcopy -i "$run/fat.img" ::/ember-hdmi-probe.log "$run/probe.log"
    grep -q 'restore.status=0x0000000000000000' "$run/probe.log"
    grep -q 'chain.load=0x0000000000000000' "$run/probe.log"
    if [ "$mode" = gop ]; then
        grep -q 'gop.before=0x0000000000000000' "$run/probe.log"
    else
        grep -q 'gop.before=0x800000000000000e' "$run/probe.log"
    fi
    printf 'PASS: %s, journal, exact loader restoration and chainloading\n' "$mode"
done
