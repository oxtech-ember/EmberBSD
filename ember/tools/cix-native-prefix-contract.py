#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD CIX native tool prefix CI contract, AI-assisted.
"""Check that CI workflows provide the native prefix CIX tests require.

Contract: a CIX test that reads EMBER_TEST_NATIVE_PREFIX runs its builder
with PATH limited to that prefix, so every workflow that runs such a test
must export the variable before the test step and provision each tool the
test and its builders take from the prefix (prefix + '/bin/NAME' in the
test, $host_prefix/bin/NAME in a builder it names, and the gnu/bin PATH
entry).  Without the variable the test must fail loudly, never skip.
Arguments are workflow files; without arguments every workflow in the
checkout is checked.  Finding no workflow or no such test is a failure.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
image = root / "ember/image"
VAR = "EMBER_TEST_NATIVE_PREFIX"
PREFIX_TOOL = re.compile(r"native_prefix \+ '/bin/([\w.+-]+)'")
GNU_PATH = re.compile(r"native_prefix \+ '/gnu/bin'")
BUILDER = re.compile(r"['\"](cix-p1-[\w.+-]+\.sh)['\"]")
HOST_TOOL = re.compile(r"\$\{?host_prefix\}?/bin/([\w.+-]+)")
RUN = re.compile(r"\bruby\s+(ember/image/test-[\w.+-]+\.rb)")
EXPORT = re.compile(rf"^\s*echo \"{VAR}=\S+\" >> \"\$GITHUB_ENV\"$|^\s+{VAR}:\s*\S",
                    re.M)
# The tool must be the destination of a link, copy or install line.
PROVIDE = r"^\s*(?:ln -s|cp|install)\b.*\s\"?\S*"
errors = []


def requirements(test):
    text = test.read_text()
    tools = set(PREFIX_TOOL.findall(text))
    for name in sorted(set(BUILDER.findall(text))):
        if (image / name).is_file():
            tools |= set(HOST_TOOL.findall((image / name).read_text()))
    return sorted(tools), bool(GNU_PATH.search(text))


tests = sorted(p for p in image.glob("test-*.rb") if VAR in p.read_text())
if not tests:
    errors.append(f"no CIX test reads {VAR}; update this contract")
workflows = [Path(a) for a in sys.argv[1:]] or \
    sorted((root / ".github/workflows").glob("*.yml"))
if not workflows:
    errors.append("no workflow to check")

checked = 0
for workflow in workflows:
    text = workflow.read_text()
    for match in RUN.finditer(text):
        test = root / match.group(1)
        if test not in tests:
            continue
        checked += 1
        where = f"{workflow.name}: {match.group(1)}"
        export = EXPORT.search(text)
        if export is None or export.start() > match.start():
            errors.append(f"{where}: {VAR} is not exported before the test")
        tools, gnu = requirements(test)
        if not tools:
            errors.append(f"{where}: no native prefix tool found; update this contract")
        for tool in tools:
            if not re.search(PROVIDE + rf"(?<!gnu/)bin/{re.escape(tool)}\"?$",
                             text[:match.start()], re.M):
                errors.append(f"{where}: native prefix does not provide bin/{tool}")
        if gnu and not re.search(PROVIDE + r"gnu/bin/\S+$", text[:match.start()], re.M):
            errors.append(f"{where}: native prefix does not provide gnu/bin")

for test in tests:
    env = {k: v for k, v in os.environ.items() if k != VAR}
    run = subprocess.run(["ruby", str(test)], env=env, capture_output=True,
                         text=True, timeout=300)
    if run.returncode == 0 or VAR not in run.stdout + run.stderr:
        errors.append(f"{test.relative_to(root)}: does not fail loudly "
                      f"without {VAR} (exit {run.returncode})")

if checked == 0 and not sys.argv[1:]:
    errors.append(f"no workflow runs a test that reads {VAR}")
if errors:
    print("\n".join(f"FAIL: {e}" for e in errors), file=sys.stderr)
    sys.exit(1)
print(f"PASS: {checked} workflow test run(s) get {VAR} with "
      f"{', '.join(requirements(tests[0])[0])}")
