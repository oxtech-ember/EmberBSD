## Problem and resulting behavior

Describe the trigger, previous result and new result. Link the change proposal if applicable.

## Scope and dependencies

Identify the owner, affected interfaces, baseline commit and required companion commits.
State any dependency on unmerged boot or firmware work.

## Validation

Tested full commit:
Host / target / configuration:
Firmware / board revision, if applicable:

| Check and command | Result | Evidence level and limits |
| --- | --- | --- |
| Failure before / success after, for a logic or behavior repair (none for a trivial fix) | | |
| Build and artifact hashes, when relevant | | |
| Shared-path regressions | | |
| Source and memory analysis, for C/C++ | | |
| VM / physical scenario, when claimed | | |

List failed checks and missing validation explicitly.
Identify the kernel actually selected by the loader.

## Provenance and review

Record imported sources, revisions, licenses and AI assistance, including plugin version when used.
Name the independent review and resolved findings, or state that review is pending.
Do not put credentials, personal logs or device-specific secrets here.

## Acceptance

- [ ] The scope and acceptance scenario match the change description.
- [ ] Relevant checks apply to the proposed revision.
- [ ] Support statements and public instructions match the evidence.
- [ ] Recovery is defined before any hardware deployment.
- [ ] Required missing validation keeps this change in draft.
