# Fixed CPU NN image

`sun60i_a733_npu_nn_tiny_image()` emits one finite byte-array image. It does
not allocate DMA, submit commands, install IRQs or access registers. There is
no hardware admission in this CPU-only function. The
[native TASK guide](a733-npu-tiny-task.md) and
[acceptance record](a733-npu-tiny-task-acceptance.json) describe the separate
fixed-profile physical result. CPU image generation alone does not prove it.

The image has 864 constants bytes, 256 input bytes and 512 expected bytes.
The 192-byte config starts at constants offset 0. A zero 64-byte gap precedes
the 608-byte coefficient block at offset 256. All words use explicit little
endian stores. Only the fixed 1632-byte output span is written. Short, null
or wrapped spans fail without changing output. Output can be unaligned.

The profile is an 8x8x4 to 8x8x8 1x1 convolution. Storage uses unsigned
bytes with zero point 128. The logical input at channel `c`, pixel `p` is
`((p*(c+1)+3*c)%11)-5`. Output kernels select channels 0..3, then the
differences 0-1, 1-2, 2-3 and 3-0. Biases are 1..8. Unit scale uses the
v8.3 float exponent 127. The input and output plane strides are 64 bytes.
The config fixes constants, input and output VAs at 0x01002000,
0x01003000 and 0x01004000. It enables no optional NN event, bypass, TP, SP,
tensor addition, activation, pooling or 40-bit address mode.

Each of eight cores gets one coefficient stream. The v8 header has version
1, zero ZRL and average bias, map 0x76543210, and eight stream sizes of 48 bits.
Each 64-byte stream encodes a little endian kernel count of one, four
signed weight differences, five zero differences, one pad byte and four
pipeline flush codes. Codes 0, 1 and 4 need no optional suffix or variable
bits. Eight little endian int32 biases follow the streams.

The format mechanism follows the MIT Mesa 26.2.4
`src/gallium/drivers/etnaviv/etnaviv_ml_nn_v8.c`. The A733 CID profile uses
`gc_feature_database.h` and `gc_hal_kernel_hardware_func_usc.h` from
OrangePi BSP commit `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`.
The fixed record selects HW_V83 and FLOAT_POST_MULT. These sources do not
by themselves establish an NBG parser contract or hardware completion.

Run `python3 ember/tools/a733-npu-nn-contract.py`. Set `CC` and
`NPU_NN_CFLAGS` for native sanitizers. The fixture compiles the actual
source. It checks all config words, streams, padding and bounds. A separate
pipeline decoder recovers the weights. An arithmetic oracle checks all
512 expected bytes. Their SHA256 is
`6b14aa6b1aba47323ba262ae69cba4dd3a970d172097e8d5730736b6bfaafedd`.

Never copy expected bytes into a DMA output page. The separate native TASK
mechanism requires NN dispatch, fence completion, terminal-gated POSTREAD, all
512 output bytes, unchanged tails and immutable inputs. An FE event, idle
state or unchanged output poison is not model acceptance.

## Finite TASK commands

`sun60i_a733_npu_nn_task_commands()` emits 144 bytes in 18 whole commands.
It uses the same fixed-span validation and tail rules. It exposes no state
address, device VA or command selector. The fixture decodes LOAD_STATE count,
offset and opcode fields, verifies the whole order, and checks the stream
SHA256 `cced6b4d080871b78f9e011fb930717ef6fc1b3926a2c57defb57534cfaa4909`.

The stream has the existing MMU flush prefix, PA/OpenCL setup, OCB and NN
configuration, and config VA 0x01002000. It then writes the two NN batch cache
flushes, the kernel GL and TS flushes, a FE-to-PE semaphore and stall, event
4 from PE, and END. The MMU token remains 0x30000701. The separate completion
token is 0x701. No ring, WAIT or LINK command is emitted.

This finite policy selects the non-2D, non-BLT branch. Mesa 26.2.4
`etnaviv_screen.c` creates the NPU pipe as 3D. The exact A733 CID record in
BSP `gc_feature_database.h` selects BltEngine 0. The kernel completion tail
follows `drivers/gpu/drm/etnaviv/etnaviv_buffer.c` from the pinned BSP commit
above. The byte encoding follows Mesa `etnaviv_emit.h`, `cmdstream.xml.h`,
`state.xml.h` and `state_3d.xml.h`. The NN dispatch and two batch flushes
follow `etnaviv_ml_nn.c` and `etnaviv_ml.c`.

This function proves no native TASK admission or cache completion. The old
FE path still refuses TASK storage. Native TASK admission must verify the
supported identity and non-BLT profile. It must also validate DMA and image
identity before publication, preserve retention, and gate result access on
the complete terminal and numerical checks. CPU stream generation does not
grant MMIO access or permit output POSTREAD.
