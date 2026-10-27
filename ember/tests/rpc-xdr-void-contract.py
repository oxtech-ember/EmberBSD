#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Use xdr_void as an xdrproc_t filter without a cast.

Use the SDK-bound CC or set HOST_CC to one absolute compiler path. The test
extracts the xdrproc_t type and the xdr_void declaration and definition from
the OS sources. A control with the upstream xdr_void(void) declaration must
fail. This is a compile and host execution model, not an RPC network test.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

OLD_DECLARATION = "extern bool_t\txdr_void(void);"
OLD_DEFINITION = "bool_t\nxdr_void(void)\n{\n\n\treturn (TRUE);\n}"

FIXTURE = """typedef int bool_t;
#define TRUE 1
#define __unused __attribute__((__unused__))
typedef struct __rpc_xdr XDR;
@TYPEDEF@
@DECLARATION@

@DEFINITION@

static const xdrproc_t assigned = xdr_void;

static xdrproc_t
cast_filter(void)
{
	return (xdrproc_t)xdr_void;
}

int
main(void)
{
	static const char data[1];

	if (assigned((XDR *)0, data) != TRUE || cast_filter()((XDR *)0, data) != TRUE)
		return 1;
	return 0;
}
"""


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def unique_line(text, prefix, name):
    lines = [line for line in text.splitlines() if line.startswith(prefix)]
    require(len(lines) == 1, "missing unique " + name)
    return lines[0]


def definition(text):
    start = text.index("\nbool_t\nxdr_void(") + 1
    end = text.index("\n}\n", start) + 2
    return text[start:end]


def main():
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[2]
    header = (root / "common/include/rpc/xdr.h").read_text()
    source = (root / "common/lib/libc/rpc/xdr.c").read_text()
    typedef = unique_line(header, "typedef\tbool_t (*xdrproc_t)", "xdrproc_t type")
    declaration = unique_line(header, "extern bool_t\txdr_void(", "xdr_void declaration")
    production = (FIXTURE.replace("@TYPEDEF@", typedef)
                  .replace("@DECLARATION@", declaration)
                  .replace("@DEFINITION@", definition(source)))
    control = (FIXTURE.replace("@TYPEDEF@", typedef)
               .replace("@DECLARATION@", OLD_DECLARATION)
               .replace("@DEFINITION@", OLD_DEFINITION))
    cc = os.environ.get("HOST_CC", os.environ.get("CC", ""))
    require(cc and Path(cc).is_absolute() and Path(cc).is_file(),
            "set HOST_CC or CC to one explicit host C compiler executable")
    version = subprocess.run([cc, "--version"], capture_output=True, text=True, timeout=30).stdout
    warning = "-Wcast-function-type-mismatch" if "clang" in version else "-Wcast-function-type"
    flags = ["-O1", "-std=c11", "-Wall", "-Wextra", "-Werror", warning]
    flags += shlex.split(os.environ.get("HOST_CFLAGS", ""))
    with tempfile.TemporaryDirectory(prefix="ember-xdr-void-") as directory:
        work = Path(directory)

        def build(name, text):
            src, binary = work / (name + ".c"), work / name
            src.write_text(text)
            result = subprocess.run([cc, *flags, str(src), "-o", str(binary)],
                                    capture_output=True, text=True, timeout=60)
            return result, binary

        result, binary = build("production", production)
        require(result.returncode == 0, "compile failed:\n" + result.stdout + result.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        require(run.returncode == 0, "xdr_void filter did not return TRUE")
        print("production: PASS")
        result, _ = build("upstream-void-control", control)
        require(result.returncode != 0, "upstream xdr_void(void) compiled without a diagnostic")
        require(warning[2:] in result.stderr, "control failed without " + warning + ":\n" + result.stderr)
        print("upstream-void-control: PASS")
    print("RPC xdr_void filter contract: PASS (" + warning + "; 1 control)")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
