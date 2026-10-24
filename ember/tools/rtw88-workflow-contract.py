#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native workflow regression contract
"""Check that the native Wi-Fi workflow sparse checkout includes its inputs."""
from pathlib import Path
import fnmatch
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
