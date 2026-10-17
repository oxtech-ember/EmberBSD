# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD rtw88 reference extraction helper, AI-assisted.
"""Copy named definitions out of the pinned reference rtw88 sources.

Used by the trace generators only; the extracted text is compiled into
a host harness and is never added to the EmberBSD tree.

  rtw88-ref-extract.py REFDIR OUT SPEC...
  SPEC is FILE:fn:NAME, FILE:data:NAME, FILE:type:NAME, FILE:enums
  or FILE:defines (macro-only copy, multi-line macros kept).
"""
import re
import sys


def lines_of(ref, path):
    return open(f"{ref}/{path}").read().split("\n")


def function(lines, name):
    pat = re.compile(r"\b" + re.escape(name) + r"\(")
    for i, line in enumerate(lines):
        if line.startswith((" ", "\t", "#", "/", "*")) or not pat.search(line):
            continue
        start = i
        # "static u8\nname(...)": the return type sits on the line above.
        if re.match(r"^" + re.escape(name) + r"\(", line):
            start = i - 1
        j = i
        while "{" not in lines[j] and not lines[j].rstrip().endswith(";"):
            j += 1
        if lines[j].rstrip().endswith(";") and "{" not in lines[j]:
            continue
        k = j
        while lines[k] != "}":
            k += 1
        return "\n".join(lines[start:k + 1])
    raise SystemExit(f"function {name} not found")


def data(lines, name):
    pat = re.compile(r"\b" + re.escape(name) + r"\s*\[")
    for i, line in enumerate(lines):
        if line.startswith((" ", "\t", "#")) or not pat.search(line):
            continue
        if line.rstrip().endswith(";") and "=" not in line:
            continue
        start = i
        if not re.match(r"^(static\s+)?const\b", line):
            start = i - 1
        k = i
        # The closing line may carry a trailing comment: "177}; /* ... */".
        while not re.search(r";\s*(/\*.*\*/)?\s*$", lines[k]):
            k += 1
        return "\n".join(lines[start:k + 1])
    raise SystemExit(f"data {name} not found")


def typedef(lines, name):
    for i, line in enumerate(lines):
        if re.match(r"^(struct|enum) " + re.escape(name) + r" \{", line):
            k = i
            while not lines[k].startswith("}"):
                k += 1
            return "\n".join(lines[i:k + 1])
    raise SystemExit(f"type {name} not found")


def enums(lines):
    out, i = [], 0
    while i < len(lines):
        if re.match(r"^enum \w+ \{", lines[i]):
            k = i
            while not lines[k].startswith("}"):
                k += 1
            out.append("\n".join(lines[i:k + 1]))
            i = k
        i += 1
    return "\n\n".join(out)


def defines(lines):
    keep, i = [], 0
    while i < len(lines):
        if lines[i].startswith("#define") and not lines[i].startswith("#define __"):
            keep.append(lines[i])
            while lines[i].rstrip().endswith("\\"):
                i += 1
                keep.append(lines[i])
        i += 1
    return "\n".join(keep)


def main():
    ref, out = sys.argv[1], sys.argv[2]
    parts = []
    for spec in sys.argv[3:]:
        path, kind, *name = spec.split(":")
        lines = lines_of(ref, path)
        if kind == "fn":
            parts.append(function(lines, name[0]))
        elif kind == "data":
            parts.append(data(lines, name[0]))
        elif kind == "type":
            parts.append(typedef(lines, name[0]))
        elif kind == "enums":
            parts.append(enums(lines))
        elif kind == "defines":
            parts.append(defines(lines))
        else:
            raise SystemExit(f"bad spec {spec}")
    with open(out, "w") as fp:
        fp.write("\n\n".join(parts) + "\n")


main()
