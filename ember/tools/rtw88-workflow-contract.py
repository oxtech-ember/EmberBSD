#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native workflow regression contract
"""Check that the native Wi-Fi workflow sparse checkout includes its inputs."""
from pathlib import Path
import fnmatch
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
workflow = root / ".github/workflows/rtw88.yml"
lines = workflow.read_text().splitlines()
try:
    marker = next(i for i, line in enumerate(lines)
                  if line.strip() == "sparse-checkout: |")
except StopIteration:
    raise SystemExit("rtw88 workflow has no sparse-checkout block")

patterns = []
for line in lines[marker + 1:]:
    if line.strip() and len(line) - len(line.lstrip()) < 12:
        break
    if line.strip():
        patterns.append(line.strip().lstrip("/"))

required = (
    ".github/workflows/rtw88.yml",
    "ember/tools/rtw88-workflow-contract.py",
    "ember/tools/rtw88-ci-shard.sh",
    "sys/net80211/ieee80211_beacon_ies.c",
    "sys/net80211/ieee80211_beacon_ies.h",
    "sys/net80211/ieee80211_vht_ies.c",
    "sys/net80211/ieee80211_vht_ies.h",
)
missing = [path for path in required
           if not any(fnmatch.fnmatchcase(path, pattern)
                      for pattern in patterns)]
if missing:
    print("rtw88 sparse checkout misses: " + ", ".join(missing), file=sys.stderr)
    raise SystemExit(1)
print(f"rtw88 sparse checkout: {len(required)} inputs covered")

# Sparse inputs must also trigger CI when changed on their own.
for event in ("push", "pull_request"):
    marker = next(i for i, line in enumerate(lines)
                  if line == "  " + event + ":")
    trigger_patterns = []
    in_paths = False
    for line in lines[marker + 1:]:
        indent = len(line) - len(line.lstrip())
        if line.strip() and indent <= 2:
            break
        if line == "    paths:":
            in_paths = True
            continue
        if in_paths and line.strip() and indent <= 4:
            break
        if in_paths and line.startswith("      - "):
            pattern = line.strip()[2:].strip("\"'")
            if pattern.startswith("!"):
                raise SystemExit("negative trigger patterns need ordered matching")
            trigger_patterns.append(pattern)
    missing = [path for path in required
               if not any(fnmatch.fnmatchcase(path, pattern)
                          for pattern in trigger_patterns)]
    if missing:
        print("rtw88 " + event + " trigger misses: " + ", ".join(missing),
              file=sys.stderr)
        raise SystemExit(1)
print(f"rtw88 push/pull_request triggers: {len(required)} inputs covered")

# Parallel shards must run every contract runner exactly once, in both
# the sanitizer and the Memcheck/analyzer steps.
text = workflow.read_text()
match = re.search(r"^        shard: \[([^]]*)\]$", text, re.M)
if match is None:
    raise SystemExit("rtw88 workflow has no contract shard matrix")
shards = [s.strip() for s in match.group(1).split(",") if s.strip()]
if len(shards) != len(set(shards)):
    raise SystemExit("rtw88 contract shards repeat")
selector = 'contracts=$(sh ember/tools/rtw88-ci-shard.sh "${{ matrix.shard }}")'
runner = ('sh "$contract" || { rc=$?; echo "::endgroup::"; '
          'echo "::error::$contract failed ($rc)"; exit "$rc"; }')
for needle in (selector, 'test -n "$contracts"',
               'for contract in $contracts; do', runner):
    if text.count(needle) != 2:
        raise SystemExit("rtw88 contract steps must run every selected "
                         "runner: " + needle)
if "for contract in ember/tools/" in text:
    raise SystemExit("rtw88 contract steps must select runners by shard")
tools = root / "ember/tools"
expected = [p.relative_to(root).as_posix()
            for p in sorted(tools.glob("rtw*-contract.sh"))]
expected += ["ember/tools/net80211-ht20-contract.sh",
             "ember/tools/net80211-rxba-contract.sh"]
owned = []
for shard in shards:
    proc = subprocess.run(["sh", str(tools / "rtw88-ci-shard.sh"), shard],
                          capture_output=True, text=True)
    if proc.returncode != 0:
        raise SystemExit("rtw88 contract shard %s: %s"
                         % (shard, proc.stderr.strip()))
    names = proc.stdout.split()
    if not names:
        raise SystemExit("rtw88 contract shard is empty: " + shard)
    owned += names
absent = [n for n in expected if not (root / n).is_file()]
if absent:
    raise SystemExit("rtw88 contract runners are absent: " + ", ".join(absent))
missing = sorted(set(expected) - set(owned))
extra = sorted(set(owned) - set(expected))
repeated = sorted({n for n in owned if owned.count(n) > 1})
if missing or extra or repeated:
    raise SystemExit("rtw88 shard coverage: missing %s extra %s repeated %s"
                     % (missing, extra, repeated))
print(f"rtw88 contract shards: {len(shards)} shards cover "
      f"{len(expected)} runners once")
