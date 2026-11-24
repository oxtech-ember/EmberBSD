# A733 fixed NN tile ioctl

Use `<arm/a733_npu_tile.h>` with the native NetBSD ioctl ABI. Open the registered
`/dev/vipcore` with read and write access. The caller must pass the kernel
superuser check. Check the current `vipmajor` before using a device node from
an older boot. Use the [fixed TASK procedure](a733-npu-tiny-task.md) to select
the matched Zero 3W kernel and complete diagnostic stages 1 through 4.
On the same boot, do not select either native diagnostic profile before the ioctl.

Call `ioctl(fd, A733_NPU_TILE_RUN, &tile)` for each tile. Fill all four input planes
with rows of eight samples. Each plane has 64 bytes. The call ignores the
output fields on entry. The logical input is the stored byte minus 128.
Choose stored values in the range 123 through 133. Prepare the image,
quantization and pixel neighborhoods in userland.

A successful call returns eight complete output planes in the same order
and layout. For each position, the first four output bytes are input
channels 0 through 3 plus biases 1 through 4. The next four output bytes
are the differences 0−1, 1−2, 2−3, and 3−0 plus biases 5 through 8 and the
output zero point 128. The kernel runs the existing fixed 1×1 command
stream and compares it with a bounded arithmetic reference. No model
file, command, address, or pointer is part of the request.

The input range and access checks precede task selection. An invalid input
returns `EINVAL`. A native FE selection returns `EBUSY`. A diagnostic TASK
selection prevents the tile ioctl for that boot. A failed initial selection
also consumes the attempt. Use the diagnostic fixture on a different cold
boot.

The ioctl keeps the exact identity, DMA mapping, IRQ and terminal checks of
the fixed TASK. It captures verified output bytes in kernel storage during
comparison and copies the snapshot to the ioctl buffer after the final
fresh hardware proof. Use output only after a successful return. A failing
userland copyout returns `EFAULT` after the driver has finished. It leaves
the completed generation and warm eligibility intact. A driver failure
can also return `EFAULT`. In an exclusive diagnostic session, compare the
new TASK IRQ baseline and the cached `task_result` and `task_model_pass`
against their values before the call. Success fields for a new generation
mean the hardware finished even when the caller could not receive output.
A driver failure after DMA publication retains the buffers, mappings,
immutable references and IRQ until reboot.
After a successful tile, the next ioctl keeps the original MMU tables,
maps, command image and IRQ. It verifies the live mask, ACK, idle state,
command DMA and MMU root before touching the input and output pages. It
checks the previous buffers, replaces the input and poisons the entire
output page. It starts the fixed command with a new IRQ-count baseline
after fresh DMA PRE operations. It requires the original terminal and
comparison checks and a fresh final hardware proof for every tile.

Uncertain completion, an unexpected IRQ, changed ownership or a bad
comparison quarantines the retained context until reboot. An invalid
ioctl request before execution leaves a completed context available.

Host simulation checks two distinct successive tiles with one map and IRQ
registration. It checks rejection of an old pending event, an event found
after enabling the mask, a changed MMU root and modified read-only memory.
The published physical acceptance used the built-in diagnostic fixture.
