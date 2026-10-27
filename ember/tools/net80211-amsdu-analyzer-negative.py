#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Require GCC to reject ownership and NULL mutations of the source contract."""

import os
from pathlib import Path
import re
import subprocess
import tempfile


tools = Path(__file__).resolve().parent
root = tools.parent.parent
env = dict(os.environ)
cc = [env["CC"]]
fixture = (tools / "net80211-amsdu-contract.c").read_text()
main = fixture.index("int\nmain(void)")
helpers = {
    "malloc-leak": """
static int negative_case(void)
{
    struct mbuf *m = newbuf();
    if (m == NULL)
        return 0;
    return m->m_len;
}
""",
    "double-free": """
static int negative_case(void)
{
    struct mbuf *m = newbuf();
    if (m == NULL)
        return 0;
    m_freem(m);
    m_freem(m);
    return 0;
}
""",
    "null-dereference": """
static int negative_case(void)
{
    struct mbuf *m = NULL;
    return m->m_len;
}
""",
    "deref-before-check": """
extern struct mbuf *negative_pointer(void);
static int negative_case(void)
{
    struct mbuf *m = negative_pointer();
    int length = m->m_len;
    if (m == NULL)
        return 0;
    return length;
}
""",
}
with tempfile.TemporaryDirectory(prefix="amsdu-analyzer-negative-") as temp:
    work = Path(temp)
    (work / "sys").mkdir()
    (work / "sys/systm.h").write_text("#include <stdint.h>\n#include <string.h>\n")
    (work / "net80211").mkdir()
    (work / "net80211/ieee80211_amsdu.h").symlink_to(root / "sys/net80211/ieee80211_amsdu.h")
    subprocess.run(["python3", str(tools / "net80211-amsdu-contract.py"),
                    str(root / "sys/net80211/ieee80211_input.c"), str(work / "native.inc")],
                   check=True, env=env)
    native = (work / "native.inc").read_text()
    variants = {}
    if os.uname().sysname == "Linux":
        variants["consumer-malloc-leak"] = (fixture, native.replace(
            "\t\t*tail = n;", "\t\t(void)*tail;", 1), "malloc-leak")
        variants["consumer-double-free"] = (fixture.replace(
            "\tdelivered++;\n\tm_freem(m);", "\tdelivered++;\n\tm_freem(m);\n\tm_freem(m);", 1),
            native, "double-free")
    else:
        print("actual consumer mutations: skipped; require Linux preprocessing paths")
    for name, helper in helpers.items():
        # Each helper has a unique location outside the extraction annotation.
        changed = fixture[:main] + '#line 1 "amsdu-negative-' + name + '.c"\n' + helper
        changed += '#line 1 "amsdu-negative-' + name + '-caller.c"\n' + fixture[main:]
        insert = changed.index("\n\treset();", changed.index("int\nmain(void)"))
        changed = (changed[:insert] + '\n\tif (getenv("AMSDU_NEGATIVE") != NULL)\n'
                   '\t\treturn negative_case();\n' + changed[insert:])
        variants[name] = (changed, native, name)
    for name, (source, body, category) in variants.items():
        location = "amsdu-negative-" + name + ".inc"
        (work / "native.inc").write_text('#line 1 "' + location + '"\n' + body)
        path = work / (name + ".c")
        path.write_text('#line 1 "amsdu-negative-' + name + '.c"\n' + source)
        command = cc + ["-std=c11", "-O0", "-fanalyzer", "--param=analyzer-max-constraints=100",
                        "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(work), "-I" + str(root / "sys/net80211"),
                        "-c", str(path), "-o", str(work / (name + ".o"))]
        result = subprocess.run(command, env=env, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        expected = "analyzer-" + category
        diagnostic = r"amsdu-negative-" + name + r"\.(?:inc|c):[^\n]*\[-Werror=" + expected + r"\]"
        if result.returncode == 0 or not re.search(diagnostic, result.stdout):
            raise SystemExit(name + ": required diagnostic missing\n" + result.stdout)
        print(name + ": required " + expected + " detected")

    # Remove only the real result guard. UBSan must catch the consuming-NULL path.
    # Diagnostic directives can occur between the condition and its body.
    missing = re.sub(r"\t\tif \(m == NULL\) \{\n(?:#.*\n)*"
                     r"\t\t\t/\* Fragment dropped or frame not complete yet \*/\n"
                     r"\t\t\tgoto out;\n\t\t}",
                     lambda match: "\n".join(line for line in match[0].splitlines()
                                               if line.startswith("#")), native)
    if missing == native:
        raise SystemExit("post-defrag guard not found")
    (work / "native.inc").write_text(missing)
    path = work / "missing-check.c"
    path.write_text(fixture)
    command = cc + ["-std=c11", "-O1", "-g", "-fsanitize=undefined",
                    "-fno-sanitize-recover=all", "-D_DEFAULT_SOURCE", "-D_DARWIN_C_SOURCE",
                    "-I" + str(work), "-I" + str(root / "sys/net80211"), str(path),
                    str(root / "sys/net80211/ieee80211_amsdu.c"), "-o", str(work / "missing-check")]
    subprocess.run(command, check=True, env=env)
    result = subprocess.run([str(work / "missing-check")], env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode == 0 or "null pointer" not in result.stdout:
        raise SystemExit("missing-check: required UBSan NULL failure missing\n" + result.stdout)
    print("missing-check: consuming-NULL path rejected by UBSan")
