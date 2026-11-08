#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD native workflow regression contract
"""Check that the native Wi-Fi workflow sparse checkout includes its inputs."""
from pathlib import Path
import fnmatch
import re
import shutil
import subprocess
import sys
import tempfile

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

# The full Git index remains available in a sparse checkout. Derive this
# family from it, so future transport units and headers also need coverage.
pci_inputs = subprocess.check_output(
    ["git", "-c", f"safe.directory={root}", "-C", str(root),
     "ls-files", "--", "sys/dev/pci/rtw88_pci_*", "sys/dev/pci/rtw88_pcie_*"],
    text=True,
).splitlines()
if not pci_inputs:
    raise SystemExit("rtw88 workflow has no tracked PCI transport inputs")

# The interface regression executes the real runners and the RX batch's
# extraction scripts even when its compiler is a recording stub. Include their
# source, headers, and generator inputs in both sparse and trigger coverage.
interface_ic = subprocess.check_output(
    ["git", "-c", f"safe.directory={root}", "-C", str(root),
     "ls-files", "--", "sys/dev/ic/rtw*", "sys/dev/sdmmc/rtw88*"],
    text=True,
).splitlines()
if not interface_ic:
    raise SystemExit("rtw88 interface regression has no tracked device inputs")
interface_inputs = (
    *interface_ic,
    "sys/dev/sdmmc/if_aicwf.c",
    "ember/tools/aicwf-rxba-contract.py",
    "ember/tools/aicwf-rxba-contract.c",
    "sys/dev/sdmmc/sdmmc_io_status.h",
    "ember/tools/rtw88-runner-interface-contract.py",
    "ember/tools/rtw88-fixture-runner.subr",
    "ember/tools/rtw88-state-contract.py",
    "ember/tools/net80211-rxba-lifecycle-extract.py",
    "ember/tools/rtw88-test-kernel.h",
    "ember/tools/rtw8723d-core-phy-order-contract.sh",
    "ember/tools/rtw8723d-core-phy-order-contract.c",
    "ember/tools/rtw8723d-core-phy-order-fixture.h",
    "ember/tools/rtw8723d-rx-batch-contract.sh",
    "ember/tools/rtw8723d-rx-batch-contract.c",
    "ember/tools/rtw8723d-legacy-ra-contract.c",
    "ember/tools/rtw88-executor-contract.sh",
    "ember/tools/rtw88-executor-contract.c",
    "ember/tools/rtw88-executor-fixture.h",
    "ember/tools/rtw88-sdio-rx-fault-contract.sh",
    "ember/tools/rtw88-sdio-rx-fault-contract.c",
    "ember/tools/rtw88-sdio-rx-fault-fixture.h",
    "sys/net/if_media.c", "sys/net/if_media.h",
    "sys/net80211/_ieee80211.h", "sys/net80211/ieee80211.h",
    "sys/net80211/ieee80211.c", "sys/net80211/ieee80211_beacon_ies.h",
    "sys/net80211/ieee80211_ht20.c", "sys/net80211/ieee80211_ht20.h",
    "sys/net80211/ieee80211_ioctl.c", "sys/net80211/ieee80211_ioctl.h",
    "sys/net80211/ieee80211_netbsd.c", "sys/net80211/ieee80211_node.c",
    "sys/net80211/ieee80211_node.h", "sys/net80211/ieee80211_phy_ies.h",
    "sys/net80211/ieee80211_proto.c", "sys/net80211/ieee80211_proto.h",
    "sys/net80211/ieee80211_rxba.c", "sys/net80211/ieee80211_rxba.h",
    "sys/net80211/ieee80211_sae.c",
    "sys/net80211/ieee80211_var.h", "sys/net80211/ieee80211_vht_ies.c",
    "sys/net80211/ieee80211_vht_ies.h",
)

required = (
    ".github/workflows/rtw88.yml",
    "ember/tools/rtw88-workflow-contract.py",
    "ember/tools/rtw88-ci-shard.sh",
    "ember/tools/rtw8821c-efuse-contract.sh",
    "ember/tools/rtw8821c-efuse-contract.c",
    "ember/tools/rtw8821c-fw-contract.sh",
    "ember/tools/rtw8821c-fw-contract.c",
    "sys/dev/ic/rtw8821c.c",
    "sys/dev/ic/rtw8821c.h",
    "sys/dev/pci/if_rtw88_pci.c",
    *pci_inputs,
    *interface_inputs,
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
unavailable = [path for path in required if not (root / path).is_file()]
if unavailable:
    raise SystemExit("rtw88 sparse worktree misses: " + ", ".join(unavailable))
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
unknown = subprocess.run(["sh", str(root / "ember/tools/rtw88-ci-shard.sh"),
                          "unknown"], capture_output=True, text=True)
if (unknown.returncode != 2 or unknown.stdout != "" or
        unknown.stderr != "unknown shard: unknown\n"):
    raise SystemExit("rtw88 shard selector did not reject an unknown shard")
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

# Clang Static Analyzer runners default CSA to clang; the workflow must
# install clang or set CSA so the analyzer always runs.
analysis_step = text.partition("      - name: Execute actual source under Memcheck and GCC path analysis\n")[2]
if not analysis_step:
    raise SystemExit("rtw88 workflow is missing its analysis step")
csa_required = [n for n in expected if "${CSA:?" in (root / n).read_text()]
if csa_required and not re.search(r"^\s+CSA:\s*clang\s*$", analysis_step, re.M):
    raise SystemExit("rtw88 required CSA runners need explicit clang in analysis step: "
                     + ", ".join(csa_required))
if "ember/tools/rtw8821c-runtime-contract.sh" in csa_required:
    if ('if [ "$contract" = ember/tools/rtw8821c-runtime-contract.sh ]; then' not in analysis_step or
            'sh "$contract" --analyze ||' not in analysis_step):
        raise SystemExit("rtw8821c runtime analysis mode is not selected in workflow")
csa_users = [n for n in expected if "${CSA:-clang}" in (root / n).read_text()]
if csa_users and not (
        re.search(r"^\s*apt-get install\b.*\sclang(?:\s|$)", text, re.M) or
        re.search(r"^\s+CSA:\s*\S", text, re.M)):
    raise SystemExit("rtw88 workflow runs CSA without clang or CSA: "
                     + ", ".join(csa_users))
print(f"rtw88 CSA: {len(csa_users)} analyzer runners have clang")

# Exercise only the declared sparse input set, even in a full checkout.
# This catches missing generator/copy inputs before the six CI shards start.
with tempfile.TemporaryDirectory(prefix="rtw88-sparse-contract-") as directory:
    sparse = Path(directory)
    for name in set(interface_inputs):
        target = sparse / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    command = [sys.executable, str(sparse / "ember/tools/rtw88-runner-interface-contract.py")]
    for name in ("sys/dev/sdmmc/sdmmc_io_status.h", "sys/dev/ic/rtw8723d_rx.c"):
        target = sparse / name
        target.unlink()
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode == 0 or target.name not in result.stderr:
            raise SystemExit("sparse interface regression missed " + name)
        shutil.copyfile(root / name, target)
    subprocess.run(command, check=True)
