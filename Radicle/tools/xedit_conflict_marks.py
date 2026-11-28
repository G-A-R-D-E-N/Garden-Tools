#!/usr/bin/env python3
"""Inventory of the conflict marks in the pinned wbDefinitionsFO4.pas (#602 D-1).

xEdit resolves a conflict through the ignore/benign/critical mark on an element,
so those marks are the missing half of the D-1 classification. This tool records
every mark site in the pinned definitions as data; it does not guess which record
uses a shared element, because a mark on a reused element applies to every record
that element is placed in.
"""

import argparse
import json
import re
from pathlib import Path

from xedit_array_inventory import pin_revision

MARK = re.compile(r"\b(cpIgnore|cpBenignIfAdded|cpBenign|cpCritical|cpOverride)\b")
RUNTIME = re.compile(r"^\s*\w*CP\s*:=\s*(cp[A-Za-z]+)\s*;")
CALL = re.compile(r"\b(wb[A-Za-z]\w*)\s*$")
SIGNATURE = re.compile(r"^\(\s*([A-Za-z_]\w{0,3})\s*,")
LABEL = re.compile(r"'([^']*)'")
CONTEXT = 60


def enclosing(text):
    # Constraint: a mark may sit on a continuation line, so ownership comes from the enclosing call, not the line.
    owner, stack = {}, []
    for index, char in enumerate(text):
        if char == "(":
            head = CALL.search(text[max(0, index - CONTEXT):index])
            stack.append((head.group(1) if head else None, index))
        elif char == ")" and stack:
            stack.pop()
        if stack and stack[-1][0] is not None:
            owner[index] = stack[-1]
    return owner


def collect(path):
    content = path.read_text(encoding="utf-8", errors="replace")
    lines = content.splitlines()
    starts = [0]
    for line in lines:
        starts.append(starts[-1] + len(line) + 1)
    owner = enclosing(content)
    definitions, runtime = [], []
    for number, line in enumerate(lines, 1):
        found = MARK.search(line)
        if found is None:
            continue
        assigned = RUNTIME.match(line)
        if assigned is not None:
            runtime.append({"line": number, "priority": assigned.group(1), "declaration": line.strip()})
            continue
        offset = starts[number - 1] + found.start()
        entry = owner.get(offset)
        if entry is None:
            raise SystemExit(f"conflict mark with no enclosing element call on line {number}: {line.strip()!r}")
        element, opening = entry
        rest = content[opening:]
        signature, label = SIGNATURE.match(rest), LABEL.search(rest)
        if signature is None and label is None:
            raise SystemExit(f"conflict mark with no signature or label on line {number}: {line.strip()!r}")
        definitions.append({
            "line": number,
            "priority": found.group(1),
            "element": element,
            "signature": signature.group(1) if signature else None,
            "label": label.group(1) if label else None,
            "declaration": line.strip(),
        })
    return definitions, runtime


def render(path, revision):
    definitions, runtime = collect(path)
    counts = {}
    for entry in definitions + runtime:
        counts[entry["priority"]] = counts.get(entry["priority"], 0) + 1
    document = {
        "version": 1,
        "source": path.name,
        "xedit_revision": revision,
        "total": len(definitions) + len(runtime),
        "definitions": len(definitions),
        "runtime": len(runtime),
        "priorities": dict(sorted(counts.items())),
        "rows": definitions,
        "runtime_rows": runtime,
    }
    return json.dumps(document, indent=1, ensure_ascii=True) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--xedit", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--revision",
                        help="xEdit commit of the definitions; defaults to the git HEAD beside the file")
    args = parser.parse_args()
    revision = args.revision or pin_revision(args.xedit)
    if not revision:
        parser.error("the definitions are not in a git checkout; pass --revision")
    text = render(args.xedit, revision)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != text:
            print(f"conflict mark inventory is stale: {args.output}")
            return 1
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
