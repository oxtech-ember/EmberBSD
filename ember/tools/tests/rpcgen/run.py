#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Check the real rpcgen preprocessor lifecycle on a POSIX host."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time


SOURCE = """\
struct payload { int value; };
program CHECK {
    version CHECK_V1 { payload FETCH(void) = 1; } = 1;
} = 0x20000001;
"""
OUTPUTS = ("proto.h", "proto_xdr.c", "proto_clnt.c", "proto_svc.c")
FAKE_CPP = """\
import json
import os
from pathlib import Path
import signal
import sys
import time

root = Path(__file__).parent
mode = root.name
records = root / 'children.json'
children = json.loads(records.read_text()) if records.exists() else []
if mode == 'multiple-success' and children:
    try:
        os.kill(children[-1], 0)
    except ProcessLookupError:
        pass
    else:
        print('previous preprocessor was not reaped', file=sys.stderr)
        sys.exit(9)
children.append(os.getpid())
records.write_text(json.dumps(children))
if mode == 'multiple-success':
    os.execv(CPP, [CPP] + sys.argv[1:])
if mode == 'multiple-failure' and len(children) == 1:
    os.execv(CPP, [CPP] + sys.argv[1:])
sys.stdout.write((root / 'proto.x').read_text())
sys.stdout.flush()
if mode == 'signal':
    os.kill(os.getpid(), signal.SIGTERM)
if mode == 'delayed-failure':
    os.close(1)
    time.sleep(0.2)
sys.exit(7)
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rpcgen", type=Path, required=True)
    parser.add_argument("--cpp", type=Path, required=True,
                        help="A single executable, with no command arguments")
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--runner", default="",
                        help="Optional command prefix, such as valgrind")
    args = parser.parse_args()
    rpcgen = args.rpcgen.resolve(strict=True)
    cpp = args.cpp.resolve(strict=True)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=False)
    results = []
    for name in ("missing", "nonzero", "signal", "delayed-failure",
                 "multiple-failure", "header-success", "multiple-success"):
        root = work / name
        root.mkdir()
        (root / "proto.x").write_text(SOURCE)
        if name == "missing":
            preprocessor = root / "absent-cpp"
        elif name == "header-success":
            preprocessor = cpp
        else:
            preprocessor = root / "cpp"
            preprocessor.write_text(
                f"#!{sys.executable}\nCPP = {str(cpp)!r}\n" + FAKE_CPP)
            preprocessor.chmod(0o755)
        command = shlex.split(args.runner) + [str(rpcgen)]
        command += (["proto.x"] if name.startswith("multiple-")
                    else ["-h", "proto.x", "-o", "proto.h"])
        environment = dict(os.environ, RPCGEN_CPP=str(preprocessor))
        start = time.monotonic()
        result = subprocess.run(command, cwd=root, env=environment,
                                capture_output=True, timeout=20)
        (root / "stdout").write_bytes(result.stdout)
        (root / "stderr").write_bytes(result.stderr)
        existing = {f: digest(root / f) for f in OUTPUTS
                    if (root / f).exists()}
        record = dict(case=name, command=command,
                      preprocessor=str(preprocessor), exit=result.returncode,
                      elapsed=time.monotonic() - start, outputs=existing)
        try:
            if name.endswith("-success"):
                assert result.returncode == 0, "rpcgen did not succeed"
                header = (root / "proto.h").read_text()
                assert "struct payload" in header, "missing generated data"
                assert "#define CHECK" in header, "missing generated program"
                if name == "multiple-success":
                    assert len(existing) == 4, "missing default output"
                    for filename, symbol in (
                            ("proto_xdr.c", "xdr_payload("),
                            ("proto_clnt.c", "fetch_1("),
                            ("proto_svc.c", "check_1(")):
                        assert symbol in (root / filename).read_text(), filename
                    children = json.loads((root / "children.json").read_text())
                    assert len(children) == 4, "wrong preprocessor scan count"
            else:
                assert result.returncode == 1, "wrong preprocessor failure status"
                assert b"C preprocessor" in result.stderr, "missing failure diagnostic"
                assert not existing, "failed output files were retained"
                if name == "multiple-failure":
                    children = json.loads((root / "children.json").read_text())
                    assert len(children) == 2, "failed scan did not stop generation"
            record["passed"] = True
        except (AssertionError, FileNotFoundError) as error:
            record.update(passed=False, error=str(error))
        results.append(record)
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'}")
    report = dict(rpcgen=dict(path=str(rpcgen), sha256=digest(rpcgen)),
                  cpp=dict(path=str(cpp), sha256=digest(cpp)), results=results)
    (work / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
