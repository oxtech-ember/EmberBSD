#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Capture analyzer evidence. Completion does not mean acceptance."""

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def record(path, argv):
    with path.open("w") as log:
        return subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT).returncode


def capture(argv):
    output = Path(os.environ["AMSDU_CAPTURE_OUTPUT"])
    compiler = json.loads(os.environ["AMSDU_CAPTURE_CC"])
    (output / "compiler-argv.json").write_text(json.dumps(compiler + argv, indent=2) + "\n")
    record(output / "gcc-v.log", compiler + ["-v"])
    work = next(Path(arg[2:]) for arg in argv
                if arg.startswith("-I") and (Path(arg[2:]) / "native.inc").is_file())
    shutil.copytree(work, output / "generated")
    # Preprocess each actual translation unit. Keep the original compiler argv.
    sources = [arg for arg in argv if arg.endswith(".c")]
    common = []
    skip = False
    for arg in argv:
        if skip:
            skip = False
        elif arg == "-o":
            skip = True
        elif arg not in sources:
            common.append(arg)
    statuses = {}
    for source in sources:
        statuses[source] = record(output / (Path(source).stem + "-preprocess.log"),
                                  compiler + common + [source, "-E", "-o",
                                  str(output / (Path(source).stem + ".i"))])
    (output / "preprocess-status.json").write_text(json.dumps(statuses, indent=2) + "\n")
    return subprocess.run(compiler + argv).returncode


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--capture":
        return capture(sys.argv[2:])
    if len(sys.argv) != 2:
        sys.exit("usage: net80211-amsdu-diagnostic.py NEW_OUTPUT_DIRECTORY")
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=False)
    tools = Path(__file__).resolve().parent
    root = tools.parent.parent
    compiler = shlex.split(os.environ.get("CC", "gcc-16"))
    # Container checkout ownership can differ. Trust only this source root.
    git = ["git", "-c", "safe.directory=" + str(root), "-C", str(root)]
    (output / "source-head.txt").write_bytes(subprocess.check_output(
        git + ["rev-parse", "HEAD"]))
    record(output / "source-status.txt", git + ["status", "--short"])
    record(output / "gcc-parameters.log", compiler + ["-Q", "--help=params"])
    variants = [("default", []), ("no-merge", ["-fno-analyzer-state-merge"]),
                ("no-purge", ["-fno-analyzer-state-purge"]),
                ("no-merge-no-purge", ["-fno-analyzer-state-merge",
                                       "-fno-analyzer-state-purge"])]
    results = []
    with tempfile.TemporaryDirectory(prefix="amsdu-diagnostic-") as temp:
        wrapper = Path(temp) / "cc"
        wrapper.write_text("#!/bin/sh\nexec " + shlex.quote(sys.executable) + " "
                           + shlex.quote(str(Path(__file__).resolve()))
                           + ' --capture "$@"\n')
        wrapper.chmod(0o755)
        for name, flags in variants:
            directory = output / name
            directory.mkdir()
            env = dict(os.environ, CC=str(wrapper), AMSDU_CAPTURE_OUTPUT=str(directory),
                       AMSDU_CAPTURE_CC=json.dumps(compiler), TEST_RUNNER="",
                       AMSDU_TEST_CFLAGS=" ".join(["-O0", "-g", "-gdwarf-4", "-fanalyzer",
                                                  "-Wanalyzer-too-complex"] + flags))
            with (directory / "run.log").open("w") as log:
                result = subprocess.run(["sh", str(tools / "net80211-amsdu-contract.sh")],
                                        env=env, stdout=log, stderr=subprocess.STDOUT)
            results.append(dict(variant=name, exit_status=result.returncode))
    (output / "results.json").write_text(json.dumps(results, indent=2) + "\n")
    hashes = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
              for path in sorted(output.rglob("*")) if path.is_file()}
    (output / "sha256.json").write_text(json.dumps(hashes, indent=2) + "\n")
    print("Diagnostic capture complete; this is not an acceptance result.")
    print(json.dumps(results, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
