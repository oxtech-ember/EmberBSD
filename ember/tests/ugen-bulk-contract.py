#!/usr/bin/env python3
"""Execute normal BULK branches from ugen.c with a deterministic USB bus."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--sanitize", action="store_true")
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
s = a.source.read_text()
constants = "\n".join(re.findall(r"^#define\s+UGEN_(?:BB|BULK_B)SIZE\s+[^\n]+", s, re.M))

def branch(name, end):
    start = s.index(name + "(struct ugen_softc *sc,")
    body = s.index("{", start)
    decl = s[body + 1:s.index("\n\tUGENHIST_FUNC();", body)]
    # Skip the existing RA/WB arm, retaining all normal-path preparation.
    bulk = s.index("\n\tcase UE_BULK:", body)
    block = s.index("{", s.index("\n\t\tif (sce->state & UGEN_BULK_", bulk))
    depth = 1
    begin = block + 1
    while depth:
        depth += (s[begin] == "{") - (s[begin] == "}")
        begin += 1
    finish = s.index("\n\tcase " + end + ":", begin)
    return decl + "\n\tswitch (0) { case 0:\n" + s[begin:finish] + "\n\t}\n\treturn error;\n"

header = (Path(__file__).with_name("ugen-bulk-fixture.c")).read_text()
code = header.replace("/* CONSTANTS */", constants)
code = code.replace("/* READ */", branch("ugen_do_read", "UE_ISOCHRONOUS"))
code = code.replace("/* WRITE */", branch("ugen_do_write", "UE_INTERRUPT"))
c = a.output / "actual.c"
c.write_text(code)
cc = shlex.split(os.environ["CC"])
command = cc + ["-std=gnu17", "-O1", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter"]
# Full function declarations include variables used only by excluded RA/WB
# arms. Baseline and candidate also need different subsets of bus helpers.
command += ["-Wno-unused-variable", "-Wno-unused-function"]
command += shlex.split(os.environ.get("CFLAGS", ""))
if a.sanitize:
    command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
command += [str(c), "-o", str(a.output / "fixture")]
subprocess.run(command, check=True)
env = os.environ.copy()
if a.sanitize:
    leaks = "0" if sys.platform == "darwin" else "1"
    env["ASAN_OPTIONS"] = "detect_leaks=" + leaks + ":halt_on_error=1:abort_on_error=1"
    env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
runner = shlex.split(os.environ.get("TEST_RUNNER", ""))
r = subprocess.run(runner + [str(a.output / "fixture")], capture_output=True, text=True, env=env)
(a.output / "stdout").write_text(r.stdout)
(a.output / "stderr").write_text(r.stderr)
result = {"compile": command, "execute": runner + [str(a.output / "fixture")],
          "exit": r.returncode, "cases": r.stdout.splitlines(), "sanitized": a.sanitize}
(a.output / "result.json").write_text(json.dumps(result, indent=2))
print(r.stdout, end="")
if r.stderr:
    print(r.stderr, end="")
raise SystemExit(r.returncode)
