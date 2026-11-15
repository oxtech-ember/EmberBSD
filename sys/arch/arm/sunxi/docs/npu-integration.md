# NPU integration interfaces

Read the [NPU architecture guide](https://github.com/oxtech-ember/EmberBSD-Docs/blob/main/modules/npu.md)
for model, runtime, compiler, and device boundaries.
This index maps the implemented native adapter and the userland extension points relevant to a new NPU.
Detailed layouts, algorithms, commands and checks remain in their linked sources.
Hardware applicability belongs to [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards).

## Existing interface boundary

There is no generic NPU driver-operations table or general model-submission API in this native path.
The current adapter exposes a [VIPLite diagnostic subset](a733-npu-vip.md)
and privileged controls for a [fixed trusted NN task](a733-npu-tiny-task.md).
Neither is a public inference-runtime backend.

The diagnostic device implements software handshake, identification queries and bounded register reads under its readiness rules.
Its memory, cache and task commands are unsupported.
The native TASK accepts no user tensor, device address, command stream or model file and exports no arbitrary output-readback API.
Do not use diagnostic initialization success as evidence of memory or execution support.

## Native implementation map

These interfaces are kernel-private and specific to the current adapter. They are examples of responsibilities, not a cross-device ABI.

| Responsibility | Interface and implementation | Contract to read |
| --- | --- | --- |
| Attach, diagnostic device, and privileged control | [sun60i_a733_npu.c](../sun60i_a733_npu.c) | [Attachment and VIPLite subset](a733-npu-vip.md): attach without hardware activation, readiness, error channels, and revocation of diagnostic access |
| Task ownership | [`sun60i_a733_npu_task_*`](../sun60i_a733_npu_task.h), [coordinator](../sun60i_a733_npu_task.c) | [Fixed TASK](a733-npu-tiny-task.md): prepare/run/status/destroy boundaries, one attempt, and retained state |
| Host fixture and CPU reference | [Fixture generator](../../../../../ember/tools/a733-npu-nn-fixture.py), [generated image](../sun60i_a733_npu_nn_data.h) | [NN fixture](a733-npu-nn.md): host-only model preparation, packing, independent decode/oracle, and regeneration |
| Trusted image and command encoding | [NN interface](../sun60i_a733_npu_nn.h), [implementation](../sun60i_a733_npu_nn.c) | [NN fixture](a733-npu-nn.md): immutable image copying and device command encoding; no general model compiler |
| DMA allocation and tensor storage | [DMA interface](../sun60i_a733_npu_dma.h), [implementation](../sun60i_a733_npu_dma.c) | [MMU/DMA](a733-npu-mmu.md), [task storage](a733-npu-engine.md): map constraints, rights, reference ownership, validation and cache synchronization |
| Device page tables | [MMU interface](../sun60i_a733_npu_mmu.h), [implementation](../sun60i_a733_npu_mmu.c) | [MMU format](a733-npu-mmu.md): bounded table construction, alias rejection, output preservation on error, and unaccepted hardware guard-fault behavior |
| Command frontend admission | [FE interface](../sun60i_a733_npu_fe.h), [implementation](../sun60i_a733_npu_fe.c) | [Native stages](a733-npu-engine.md): cold-state checks and finite trusted command images |
| Execution, IRQ and terminal proof | [Run interface](../sun60i_a733_npu_run.h), [implementation](../sun60i_a733_npu_run.c) | [Native stages](a733-npu-engine.md) and [fixed TASK](a733-npu-tiny-task.md): completion tied to the run and DMA identity, late errors and final proof |
| Clock/reset and power ownership | [Clock provider](../sun60i_a733_ccu.h), [clock contract](a733-accelerator-clocks.md), [power contract](a733-power-domains.md) | Typed reservations and retained ownership; NPU operations must preserve peer GPU/display resources |

`task_prepare` creates only the trusted image, command stream and owned storage; it does not submit work.
The caller must preserve a returned context when constructor cleanup fails, as specified in the task header.
The run path retains hardware-visible assets before publication and gates output access on completion for the exact task.
The coordinator compares results and then requests a final live proof; comparison alone is not model success.

Once publication is attempted, task state, mappings, IRQ storage and references remain retained until reboot,
including after successful execution. A dry control request is not reset.
Repeated submission and release of published work are not supported by this contract.
The existing `destroy` entry does not establish reusable session teardown or cancellation.

## Shared OS mechanisms for a new adapter

Reuse the native mechanism for the device's actual attachment and execution ABI.

| Need | Existing owner | Integration requirement |
| --- | --- | --- |
| Registers and DMA | [bus_space(9)](../../../../../share/man/man9/bus_space.9), [bus_dma(9)](../../../../../share/man/man9/bus_dma.9) | Register bounds, barriers, DMA address/segment constraints, explicit synchronization and map lifetime |
| Platform resources | [FDT interfaces](../../../../dev/fdt/fdtvar.h), [PCI interfaces](../../../../dev/pci/pcivar.h), [power-management framework](../../../../../share/man/man9/pmf.9) | Select the actual bus, IRQ, reset and power path; report supported lifecycle operations honestly |
| Device access | [Native device switch](../../../../sys/conf.h) and the selected driver's control implementation | Authenticate requests, validate copied input and object ownership, and define per-open lifetime before exposing untrusted submissions |
| DRM-based execution, where the upstream ABI uses it | [DRM driver interface map](../../../../external/bsd/drm2/drm/docs/README.md) | Reuse applicable buffer, scheduler and fence mechanisms only with their implemented semantics; the current native NPU task is not a DRM device |

An NPU with a different bus or firmware protocol needs its actual adapter.
Linux ioctl numbers, `/dev/accel` naming, or a vendor library ABI do not themselves create native EmberBSD support.
Preserve source licensing and attribution when adapting upstream code.

## Userspace integration

Keep common runtime sources, dependencies and portability patches in Ports.
Build tools that prepare models execute on their declared host; target libraries execute on EmberBSD.
Do not treat a source-only package, simulator, or no-device test as an installed NPU backend.

| Entry point | Source owner | Work for an NPU backend |
| --- | --- | --- |
| ONNX Runtime provider | [Recipe](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/pkgsrc/local-ai/onnxruntime), [features](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/local-ai/onnxruntime/features.cmake), [options](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/local-ai/onnxruntime/options.mk) | Match the selected runtime API and install the provider with its dependencies. The current options select WebGPU, not a native NPU provider. |
| New ONNX Runtime plugin | Upstream [plugin EP development](https://onnxruntime.ai/docs/execution-providers/plugin-ep-libraries/development.html) and [provider integration](https://onnxruntime.ai/docs/execution-providers/add-execution-provider.html) | Implement the factory/device selection and `OrtEp` capability/execution path for the chosen API version. For a compiling provider, supply graph compilation and computation ownership. Use the upstream plugin route rather than assuming an in-tree provider is required. |
| LiteRT compiler and dispatch | [Shared source recipe](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/devel/litert/Makefile); upstream [compiler interface](https://github.com/google-ai-edge/LiteRT/blob/v2.3.2/litert/vendors/c/litert_compiler_plugin.h), [dispatch interface](https://github.com/google-ai-edge/LiteRT/blob/v2.3.2/litert/vendors/c/litert_dispatch.h) | Partition/compile supported work and connect device contexts, buffers, invocation and completion through a compatible runtime. These upstream interfaces are not implemented by the native TASK. |
| LiteRT CPU consumer | [CPU profile](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/litert/README.md), [CMake selection](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/litert/CMakeLists.txt) | This profile disables NPU. Its CPU tests are references for runtime behavior, not NPU execution evidence. |
| Mesa delegate path | Upstream [Teflon](https://docs.mesa3d.org/teflon.html), [shared Mesa recipe](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/pkgsrc/graphics/MesaLib/Makefile) | Requires a compatible Gallium NPU backend and native kernel transport. The reviewed recipe disables Teflon; enabling one option alone would not complete the port. |
| Compass device library | [Probe and provenance](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/probes/compass-umd), [full UMD contract](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/compass-umd/full-umd.md) | Preserve the matched KMD/UMD wire layout and validate capabilities against the target. The current probe has no native kernel transport or model execution. |

The LiteRT headers above are pinned to the reviewed shared source version. Recheck the recipe and matching headers when implementing a backend.
Model parsing, graph lowering, quantization, weight packing and application reference outputs belong to userland or test tools.
Hardware-specific command validation and execution remain in the adapter that owns the device protocol.
Compiler-produced input is still untrusted when submitted by an application.

## Extending beyond the fixed task

The following are requirements for a new usable integration, not APIs supplied by the current native task.
Choose the actual runtime and upstream device contract first. Keep exact definitions and checks beside their implementation.

| Contract to establish | Required boundary |
| --- | --- |
| Discovery and capabilities | Exact compatible device identity, command/firmware ABI, supported operations, types, shapes, layouts and memory limits; preserve the distinction between reported and validated capabilities |
| Context and buffers | Process/object ownership, allocation/import rights, offsets and bounds, CPU/device mappings, cache visibility, and references retained by outstanding work |
| Submission | Validate copied requests and referenced storage before publication; define dependency capture, admission limits, accepted work and explicit unsupported operations |
| Completion | Bind status to the right context/job generation; distinguish acceptance, execution completion and valid output; handle stale events and device faults |
| Cancel, timeout, close and reset | Define which work can stop, how DMA is quiesced or isolated, when buffers may be reused, and how pending consumers receive errors |
| Runtime capability selection | Match full operator semantics and tensor contracts; reject unsupported cases or use a declared runtime alternative, with observable backend selection |

Put reusable scheduling, validation, cancellation and ownership logic at the common owner before adding another adapter.
Do not copy the fixed fixture's permanent retention into a general service without a deliberate lifecycle contract.
Conversely, do not free its retained memory merely because a new consumer wants repeated inference.

## Validation entry points

The existing [NPU source-level test runner](../../../../../ember/tests/a733-npu-observe.py) and focused checks exercise the actual native implementation with test doubles:

- [MMU](../../../../../ember/tools/a733-npu-mmu-contract.py) and [DMA](../../../../../ember/tools/a733-npu-dma-contract.py): bounds, aliases, mappings, references and cleanup.
- [NN fixture](../../../../../ember/tools/a733-npu-nn-contract.py): regeneration, encoding and an independent numerical oracle.
- [Frontend](../../../../../ember/tools/a733-npu-fe-contract.py), [run](../../../../../ember/tools/a733-npu-run-contract.py), and [TASK](../../../../../ember/tools/a733-npu-task-contract.py): admission, publication, IRQ/terminal proof, late errors, and retained ownership.
- [Compass checks](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/probes/compass-umd/README.md): userspace descriptor lifetime, capability bounds and no-device behavior.

Use the linked contracts for exact commands and select the SDK/Ports tools for the changed inputs.
Software tests do not establish device execution, guard-fault isolation, reset recovery or model compatibility.
A new application path additionally needs numerical comparisons, rejected/unsupported inputs, process-close behavior,
repeated buffer reuse, concurrency where supported, and the intended runtime consumer.
Measure preprocessing, compilation, transfers, execution and postprocessing separately before reporting end-to-end cost.
Keep physical applicability and acceptance in DevBoards.
