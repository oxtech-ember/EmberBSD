#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD Clang Static Analyzer CI contract, AI-assisted.
"""Check that CI workflows and net80211 scripts agree on the CSA contract.

Contract: an analyzer script invokes "${CSA:-clang}" (Python:
os.environ.get('CSA') or 'clang'), never a required-only CSA, and every
workflow that runs such a script installs clang or sets CSA.  The selected
analyzer must reject a causal NULL dereference and accept a clean unit.
Arguments are workflow files; without arguments every workflow in the
checkout is checked.  Finding no workflow is a failure, not a skip.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
tools = root / "ember/tools"
REQUIRED = re.compile(r"\$\{CSA:\?[^}'\"\n]+\}|environ\[['\"]CSA['\"]\]")
USES = re.compile(r"\$\{CSA:-clang\}|environ\.get\(['\"]CSA['\"]\) or ['\"]clang['\"]")
RUN = re.compile(r"\b(?:sh|python3)\s+(ember/(?:tools|tests)/[\w.+-]+)")
INSTALL = re.compile(r"^\s*apt-get install\b.*\sclang(?:\s|$)", re.M)
CSA_ENV = re.compile(r"^\s+CSA:\s*\S", re.M)
errors = []

scripts = sorted(p for p in tools.iterdir()
                 if p.suffix in (".sh", ".py") and p.name != Path(__file__).name)
for path in scripts:
    if REQUIRED.search(path.read_text(errors="replace")):
        errors.append(f"{path.relative_to(root)}: CSA must default to clang")

workflows = [Path(a) for a in sys.argv[1:]] or \
    sorted((root / ".github/workflows").glob("*.yml"))
if not workflows:
    errors.append("no workflow to check")


def shard_runners(text):
    match = re.search(r"^\s+shard: \[([^]]*)\]$", text, re.M)
    if "rtw88-ci-shard.sh" not in text or match is None:
        return []
    names = []
    for shard in (s.strip() for s in match.group(1).split(",")):
        out = subprocess.run(["sh", str(tools / "rtw88-ci-shard.sh"), shard],
                             capture_output=True, text=True, check=True)
        names += out.stdout.split()
    return names


checked = 0
for workflow in workflows:
    text = workflow.read_text()
    runners = sorted(set(RUN.findall(text) + shard_runners(text)))
    users = [r for r in runners
             if (root / r).is_file() and USES.search((root / r).read_text())]
    checked += len(users)
    if users and not (INSTALL.search(text) or CSA_ENV.search(text)):
        errors.append(f"{workflow.name}: runs CSA scripts without clang or "
                      f"CSA: {', '.join(users)}")

csa = os.environ.get("CSA") or "clang"
if shutil.which(csa) is None:
    errors.append(f"no Clang Static Analyzer: {csa!r} not found; "
                  "set CSA or install clang")
else:
    with tempfile.TemporaryDirectory(prefix="csa-contract-") as work:
        bad = Path(work, "null.c")
        bad.write_text("void csa_causal_null(void) { int *p = 0; *p = 1; }\n")
        good = Path(work, "clean.c")
        good.write_text("int csa_clean(int v) { return v + 1; }\n")
        argv = [csa, "--analyze", "-Xanalyzer", "-analyzer-output=text",
                "-Xanalyzer", "-analyzer-werror"]
        rejected = subprocess.run(argv + [str(bad), "-o", work + "/null.plist"],
                                  capture_output=True, text=True)
        if rejected.returncode == 0 or "core.NullDereference" not in rejected.stderr:
            errors.append("CSA did not reject the causal NULL dereference: "
                          + rejected.stderr)
        accepted = subprocess.run(argv + [str(good), "-o", work + "/clean.plist"],
                                  capture_output=True, text=True)
        if accepted.returncode != 0:
            errors.append("CSA rejected a clean unit: " + accepted.stderr)

if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print(f"CSA contract: {len(scripts)} scripts have no required-only CSA; "
      f"{len(workflows)} workflows provide clang for {checked} analyzer runs; "
      f"{csa} rejects causal NULL")
