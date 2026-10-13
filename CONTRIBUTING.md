# Change EmberBSD

Use this procedure for shared kernel fixes, board ports, boot changes, and documentation.
It applies to every contributor, including maintainers and the project owner.
An AI assistant follows the same acceptance requirements as a human developer.

A personal patch can remain local. Follow the preparation and validation steps.
Publication is required only when you submit it as a shared contribution.

## 1. Select the owner

| Change | Accepting repository |
| --- | --- |
| Kernel, drivers, board support, OS boot or firmware integration | [EmberBSD](https://github.com/oxtech-ember/EmberBSD) |
| Third-party recipes, portability patches or shared toolchains | [EmberBSD-Ports](https://github.com/oxtech-ember/EmberBSD-Ports) |
| Applications and standalone demonstrations | [EmberBSD-Examples](https://github.com/oxtech-ember/EmberBSD-Examples) |
| Planned application interfaces or execution contracts | [SDK](https://github.com/oxtech-ember/EmberBSD-SDK) / [Runtime](https://github.com/oxtech-ember/EmberBSD-Runtime); verify that the required implementation exists |
| Developer skills, client adapters and plugin packaging | [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills) |
| Device-operation and personal-change skills | [EmberBSD-User-Skills](https://github.com/oxtech-ember/EmberBSD-User-Skills) |

Use the accepting repository's current instructions and license.
Keep a shared library fix in Ports rather than each application.
Split changes across owners and state the required commits or interface between them.
SDK and Runtime names do not establish released APIs.

## 2. Describe the change before implementation

Search [EmberBSD issues](https://github.com/oxtech-ember/EmberBSD/issues) and existing pull requests.
Use the owning repository's issue tracker for a component change.
Open or update an issue for a new board, substantial feature, or interface change.
Discuss its scope and acceptance scenario before implementation.
A small repair can put the same information directly in its pull request.

Record these fields in the change description:

| Field | Required content |
| --- | --- |
| Problem and expected result | Reproduction steps, current result, and the intended observable behavior |
| Scope | Owner, affected paths/interfaces, and excluded work |
| Baseline | Full source commit; relevant dependency and firmware versions |
| Target | Host, architecture, kernel configuration; board model/revision when applicable |
| Acceptance | Checks, expected outputs, comparison baseline, and required execution platform |
| Integration | Dependencies on other changes; agreed source or artifact contract |
| Recovery | How to return a test device to its accepted state before deployment |

For a board, use [the board-change worksheet](ember/development/board-change.md).
For kernel work, use [the kernel guide](ember/development/kernel.md).
Do not treat a proposal or a source file as tested support.

## 3. Prepare an isolated revision

Inspect the checkout before changing it. Run these commands from the repository root:

```sh
git status --short
git branch --show-current
git fetch origin
git log -1 --format='%H %s' origin/main
git worktree list
```

Preserve existing local changes and commits. Do not switch a shared checkout's branch.
Set `worktree` to a new directory outside the source checkout.
Set `branch` to your own unused topic name:

```sh
worktree=/absolute/new-worktree
branch=change/my-fix
git worktree add -b "$branch" "$worktree" origin/main
cd "$worktree"
git rev-parse HEAD
```

Record that full baseline commit in the change description.
Fetch again before integration. Inspect new changes before merging them into your topic.
Never reset or overwrite another contributor's work.

Parallel boot work is not a dependency merely because it exists.
Identify the exact loader, firmware, image layout, and kernel handoff you need.
Use the accepted baseline unless a reviewed dependency requires another revision.
Keep an unmerged loader change separate and label any combined test accordingly.

## 4. Implement and verify

Keep each commit focused on one change. Write public code and prose in English.
Use Conventional Commits and NetBSD KNF for kernel C.
Preserve copyright, licenses, upstream identifiers, and imported revisions.
Disclose AI assistance and the origin of adaptations.
Do not copy GPL implementation into BSD source without a compatible license.

For a repair, reproduce the failure before the fix.
Add a regression that checks the actual cause and passes after the fix.
For a new feature, test its success, failure, and cleanup paths.
For a port, test the build, boot integration, and the claimed device scenario.
For documentation, check links and commands; do not require a kernel build for prose changes.

Build from a clean commit. Record source, configuration, tools, commands, and artifact hashes.
Run relevant shared-path regressions on an existing configuration.
Keep kernel, modules, firmware and userland compatible.
Never accept leftover artifacts after a failed build.

Use Cppcheck and applicable GCC/Clang path analysis for changed C/C++ sources and tests.
Use actual target headers and compiler settings.
Run executable contracts with Valgrind on a supported platform.
Record unavailable tools and use appropriate sanitizers or kernel diagnostics.
Valgrind cannot directly validate a running kernel or device DMA.
See [kernel validation](ember/development/kernel.md#validate-the-change) for these boundaries.

| Evidence | What it establishes |
| --- | --- |
| Source inspection | Code and integration paths exist |
| Build | Named inputs produce the recorded artifacts |
| Software contract | The stated behavior passes in the test model |
| VM | The named guest scenario executes |
| Physical device | The stated function works on that model, firmware and configuration |
| Sustained operation | The recorded workload runs for the stated duration |

Name failures and missing checks. Do not replace them with a generic “tests passed”.
Keep secrets, private addresses, full personal logs, and personalized images out of public material.

## 5. Review and integrate

Prepare a focused change description using [the PR template](.github/PULL_REQUEST_TEMPLATE.md).
Provide the tested full commit, commands, results, evidence limits, and provenance.
For an external contribution, publish your topic to your fork and open a PR.
Select the accepting repository and `main` base explicitly.
Do not send a fork fix to the NetBSD mirror by mistake.

Maintainers can keep topic branches local until a PR, shared work, or server CI requires publication.
Where protection permits direct integration, the same checks and independent review still apply.
Direct push permission does not waive acceptance, including for the project owner.
Push only the intended branch. Never use force-push to overwrite someone else's work.

A reviewer other than the implementer checks the complete diff, provenance, tests, and claims.
Fix significant findings before integration and rerun the affected checks.
If the final revision changes, state which evidence remains applicable and rerun checks it invalidates.
CI must finish successfully for the proposed final head when CI is required.
A green earlier commit is not a green final commit.

Use a draft while required validation is missing.
A reviewed source foundation can be integrated with explicit limits if maintainers accept that scope.
It does not make a board “tested”. Update its capability table only from corresponding evidence.
Keep hardware writes and restarts within the agreed device scope.

## 6. Record the resulting state

| State | Meaning |
| --- | --- |
| Prepared | A local patch and its evidence exist |
| Submitted | The accepting project has the actual PR or patch |
| Reviewed | Independent review findings are resolved for the proposed revision |
| Merged | The accepting branch contains the change |
| Released | An identified distributed build contains it |

Update the public overview and the owning component guide when behavior or support changes.
Record the integration SHA; retain the mapping if commits are squashed.
Do not call a merged patch a published release or a physical-device acceptance.

## Development through agents

Use **EmberBSD Development** from [Ember-Agent-Skills](https://github.com/oxtech-ember/Ember-Agent-Skills).
The plugin supplies task instructions. It does not supply a device runtime or credentials.
Manual development does not require it; agent-assisted development should select its relevant skill.

| Procedure step | Skill |
| --- | --- |
| Choose the owner and public instructions | `emberbsd-repository-guide` |
| Add or qualify a board | `emberbsd-add-board` |
| Prepare GCC16 tools and build a kernel | `emberbsd-build-kernel` |
| Build a static base set | `emberbsd-build-static-base` |
| Repair shared dependencies | `emberbsd-port-software` |
| Build an application or example | `emberbsd-develop-app` |
| Submit an authorized shared change | `emberbsd-contribute` |
| Change the plugin itself | `emberbsd-maintain-skills` |

Follow [plugin installation and update instructions](https://github.com/oxtech-ember/Ember-Agent-Skills#use-in-your-development-environment).
For Codex CLI with plugin support:

```sh
codex plugin marketplace add oxtech-ember/Ember-Agent-Skills --ref main
codex plugin list --marketplace ember-agent-skills --available --json
codex plugin add emberbsd-development@ember-agent-skills
```

Verify discovery in the client and record the plugin version with the change.
Reading a skill manually does not install it or prove agent behavior.
Start the task with the same problem, baseline, acceptance scenario, and deployment scope used above.
For example:

> Use emberbsd-repository-guide and emberbsd-add-board for the approved board-change issue.
> Follow CONTRIBUTING.md. Record source/build/VM/hardware separately.
> Preserve parallel boot work. Prepare the patch and evidence for independent review.
> Do not flash or restart the board outside the agreed scope.

The implementer's agent output is not an independent review.
A separate reviewer must inspect the patch and evidence.
An agent cannot approve its own missing hardware checks.

## Upstream NetBSD

Prepare upstream work separately against NetBSD's accepting revision and submission channel.
Use its [problem reports](https://www.netbsd.org/support/send-pr.html) or relevant technical mailing list.
Recheck [commit guidelines and AI policy](https://www.netbsd.org/developers/commit-guidelines.html) before submission.
NetBSD requires prior written core approval to commit LLM-generated code.
Disclose assistance; an EmberBSD review or merge does not supply that approval.
Do not automatically submit a user's patch upstream.
