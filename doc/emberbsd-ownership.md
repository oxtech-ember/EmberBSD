# Source and documentation ownership

Choose the owner before adding a file. A similar existing path is not proof of ownership.
The OS repository is not an umbrella documentation repository for the ecosystem.

| Content | Canonical owner |
| --- | --- |
| Kernel, base userland, drivers, loader code and native build targets | EmberBSD, in the existing subsystem |
| Software API, resource lifetime, errors and executable regression | Beside that implementation; link the test entry point |
| Boards, SoCs, carriers, power, wiring, loader requirements and media layouts | [DevBoards](https://github.com/oxtech-ember/EmberBSD-DevBoards) |
| Current hardware support, installation applicability and scoped physical evidence | The existing DevBoards profile, with source pins and limits |
| Shared host orchestration, sysroots, release composition and publication | [SDK](https://github.com/oxtech-ember/EmberBSD-SDK) |
| Third-party packages, toolchains, dependencies and portability patches | [Ports](https://github.com/oxtech-ember/EmberBSD-Ports) |
| Standalone applications, desktop interfaces and demonstrations | Their application repository or [Examples](https://github.com/oxtech-ember/EmberBSD-Examples) |
| Agent scenario selection | [Developer skills](https://github.com/oxtech-ember/Ember-Agent-Skills) or [user skills](https://github.com/oxtech-ember/EmberBSD-User-Skills), with links to owner documentation |
| General OS use | [User guide](guide/user-guide.md); device-specific applicability stays in DevBoards |

## Keep one current description

Update the existing owner document. Do not copy its support matrix, CLI reference or package versions into the OS README.
The root README provides purpose and navigation. Change it when an entry point, owner or product boundary changes.
A software contract states current behavior, prerequisites, checks and limits.
A hardware claim states the tested model/revision, inputs, scenario and untested functions in DevBoards.
Source, build, VM and physical results are different evidence levels.

Do not publish test diaries, reviewer conversations, task plans or sequences of failed attempts as current documentation.
Remove completed assistant instructions from design documents. Describe the current mechanism and checks.
Keep useful source pins, hashes and measured limits with their owner. Git retains the evolution.
Preserve immutable receipts and licensed source provenance. Do not rewrite old results as current acceptance.
Place a proposal in the receiving project's supported discussion or change description, not a permanent OS document.

## The ember integration directory

`ember/boot` contains consumed boot scripts and assets, not arbitrary driver or application guides.
`ember/tools` and `ember/tests` contain OS-specific integration tools and checks that consume this source tree.
Their presence does not make shared build orchestration or an application an OS component.
`ember/image` contains low-level OS root assembly and startup integration. Hardware deployment applicability belongs to DevBoards.
`ember/firmware` contains the applied firmware adaptation series and its build contract.
`ember/patches` and `ember/source.json` retain adaptation provenance. These patches are already applied; they are not a second live implementation.
Kernel configurations belong in `sys/arch/*/conf`.

## Move a component completely

Find callers, CI paths, tests and pinned consumers before moving it.
Publish the checked destination first. Update live references and consumers before removing the duplicate.
Keep historical commit URLs as provenance. Live main links must resolve to the current owner.
Preserve behavior, license, authorship and receipt compatibility. A new path is not a new build or hardware result.
Run the affected checks and inspect the combined change before publication.
