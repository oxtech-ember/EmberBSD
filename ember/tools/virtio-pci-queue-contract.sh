#!/bin/sh
# Origin: EmberBSD native VirtIO PCI queue teardown contract, 2026-10-08.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/virtio-pci-queue.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v declaration="$3" '
        $0 ~ "^" name "\\(" { copying = 1; print declaration }
        copying { print; if (/^}/) { found = 1; exit } }
        END { if (!found) exit 1 }
    ' "$2"
}
pci="$src/sys/dev/pci/virtio_pci.c"
native="$src/sys/dev/pci/virtio.c"
cp "$src/sys/dev/pci/virtio_pcireg.h" "$work/virtio_pcireg.h"
{
    extract virtio_pci_bus_space_write_8 "$pci" 'static void'
    extract virtio_pci_setup_queue_09 "$pci" 'static void'
    extract virtio_pci_setup_queue_10 "$pci" 'static void'
    extract virtio_child_detach "$native" 'static void'
    extract virtio_free_vq "$native" 'static int'
} > "$work/queue-under-test.h"
for assertions in 0 1; do
    # CFLAGS may supply sanitizers; assertions in the fixture stay enabled.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -DKERNEL_ASSERTIONS="$assertions" -I"$work" \
        "$src/ember/tools/virtio-pci-queue-contract.c" -o "$work/contract"
    "$work/contract"
done

# Mutate only generated production bodies, never the checkout.  Every
# mutant must compile, then fail the same runtime assertions as the fix.
cp "$work/queue-under-test.h" "$work/queue-good.h"
for mutation in dereference wrong-index skip-disable wrong-offset skip-drain; do
    awk -v mutation="$mutation" '
        mutation == "dereference" {
            changed += sub(/VIRTIO_CONFIG1_QUEUE_SELECT, idx/,
                "VIRTIO_CONFIG1_QUEUE_SELECT, sc->sc_vqs[idx].vq_index")
        }
        mutation == "wrong-index" {
            changed += sub(/VIRTIO_CONFIG1_QUEUE_SELECT, idx/,
                "VIRTIO_CONFIG1_QUEUE_SELECT, 0")
        }
        mutation == "skip-disable" {
            changed += sub(/VIRTIO_CONFIG1_QUEUE_ENABLE, 0/,
                "VIRTIO_CONFIG1_QUEUE_ENABLE, 1")
        }
        mutation == "wrong-offset" {
            changed += sub(/addr \+ vq->vq_availoffset/,
                "addr + vq->vq_usedoffset")
        }
        mutation == "skip-drain" && /sc->sc_ops->free_interrupts\(sc\);/ {
            $0 = "/* Interrupt drain deliberately removed. */"; changed++
        }
        { print }
        END { if (changed != 1) exit 1 }
    ' "$work/queue-good.h" > "$work/queue-under-test.h"
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror ${CFLAGS:-} \
        -DKERNEL_ASSERTIONS=0 -I"$work" \
        "$src/ember/tools/virtio-pci-queue-contract.c" -o "$work/mutant"
    if (ulimit -c 0; "$work/mutant") > "$work/mutant.log" 2>&1; then
        echo "surviving VirtIO PCI queue mutant: $mutation" >&2
        exit 1
    fi
    echo "VirtIO PCI queue causal mutant rejected: $mutation"
done
