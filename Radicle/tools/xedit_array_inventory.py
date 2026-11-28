"""Derive the xEdit counted-array and sorted-array inventories (#602 D-26 D-25).

Reads the pinned Core/wbDefinitionsFO4.pas and emits two checked-in JSON
inventories so the schema can be validated against them without the xEdit
source tree:

  schema/fallout4/xedit_count_paths.json
  schema/fallout4/xedit_sorted_arrays.json

Each document records the definitions file and the xEdit commit it was read
from, and an entries list. Each entry records the definition line, the raw
declaration, and a normalized model. Entries the parser cannot model
faithfully are emitted with "model": "unsupported" and a reason instead of a
guess. The element model comes from the element argument of the array call.
"""

import argparse
import json
import re
import subprocess
from pathlib import Path

ARRAY_OPENERS = ("wbArrayS(", "wbRArrayS(", "wbArray(", "wbRArray(")
SIGNATURE = r"'?\b([A-Z][A-Z0-9_]{3})\b'?"
COUNT_CALL = re.compile(r"SetCountPath(?:OnValue)?\(([^)]*)\)")
ARRAY_CALL = re.compile(r"\b(wbR?ArrayS?)\(")
STRUCT_SK = re.compile(r"wbStructSK\(\s*(?:[A-Z][A-Z0-9_]{3}\s*,\s*)?\[([0-9,\s]+)\]")
SIGNATURE_ONLY = re.compile(r"[A-Z][A-Z0-9_]{3}")
ASSIGNMENT = re.compile(r"^[ \t]*(wb\w+)\s*:=\s*", re.MULTILINE)
FORMID_ELEMENTS = ("wbFormIDCk", "wbFormIDCK")
ARGUMENT_WINDOW = 80
DEFINITION_WINDOW = 400


def normalize_count_target(raw):
    raw = raw.strip()
    if raw.endswith(", False") or raw.endswith(", True"):
        raw = raw.rsplit(",", 1)[0].strip()
    if raw.startswith("'") and raw.endswith("'"):
        return {"kind": "named_path", "path": raw.strip("'")}
    if re.fullmatch(r"[A-Z][A-Z0-9_]{3}", raw):
        return {"kind": "subrecord", "subrecord": raw}
    return None


def enclosing_array(lines, index):
    """Walk back from a SetCountPath call to the array declaration it decorates."""
    depth = 0
    for back in range(index, max(-1, index - 40), -1):
        text = lines[back]
        depth += text.count(")") - text.count("(")
        if depth > 0:
            continue
        match = ARRAY_CALL.search(text)
        if match and text.strip().startswith(("wb", "n", "a")):
            return back, match.group(1)
        if text.strip().startswith(("wb", "n")) and ARRAY_CALL.search(text):
            return back, ARRAY_CALL.search(text).group(1)
    return None, None


def call_arguments(text, open_at):
    """Top-level arguments of the call whose opening parenthesis is at text[open_at]."""
    args, current, depth, i = [], [], 0, open_at
    while i < len(text):
        ch = text[i]
        if ch == "'":
            j = i + 1
            while j < len(text):
                if text[j] == "'":
                    if text[j + 1:j + 2] == "'":
                        j += 2
                        continue
                    break
                j += 1
            current.append(text[i:j + 1])
            i = j + 1
            continue
        if text.startswith("//", i):
            newline = text.find("\n", i)
            i = len(text) if newline < 0 else newline
            continue
        if ch == "{":
            close = text.find("}", i)
            i = len(text) if close < 0 else close + 1
            continue
        if ch in "([":
            depth += 1
            if depth > 1:
                current.append(ch)
        elif ch in ")]":
            depth -= 1
            if depth == 0:
                args.append("".join(current).strip())
                return args
            current.append(ch)
        elif ch == "," and depth == 1:
            args.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
        i += 1
    return args


def element_argument(args):
    """The element definition of an array call: after the optional signature and the name."""
    rest = list(args)
    if rest and SIGNATURE_ONLY.fullmatch(rest[0]):
        rest = rest[1:]
    return rest[1] if len(rest) > 1 else ""


def element_kind(expression, definitions, depth=0):
    head = re.match(r"\w+", expression)
    if head is None:
        return ("unsupported", None)
    if head.group(0) in FORMID_ELEMENTS:
        return ("formid", None)
    struct = STRUCT_SK.match(expression)
    if struct:
        keys = [int(part) for part in struct.group(1).replace(" ", "").split(",") if part != ""]
        return ("struct_key", keys)
    if head.group(0) == expression and expression in definitions and depth < 4:
        return element_kind(definitions[expression], definitions, depth + 1)
    return ("unsupported", None)


def named_definitions(text):
    """First assignment of each wb helper, as the start of its right-hand side."""
    found = {}
    for match in ASSIGNMENT.finditer(text):
        found.setdefault(match.group(1), text[match.end():match.end() + DEFINITION_WINDOW])
    return found


def pin_revision(definitions_path):
    result = subprocess.run(
        ["git", "-C", str(Path(definitions_path).resolve().parent), "rev-parse", "HEAD"],
        capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--definitions", required=True)
    parser.add_argument("--count-output", required=True)
    parser.add_argument("--sorted-output", required=True)
    parser.add_argument("--revision",
                        help="xEdit commit of the definitions; defaults to the git HEAD beside the file")
    args = parser.parse_args()

    revision = args.revision or pin_revision(args.definitions)
    if not revision:
        parser.error("the definitions are not in a git checkout; pass --revision")
    full_text = Path(args.definitions).read_text(encoding="utf-8", errors="replace")
    lines = full_text.splitlines()
    definitions = named_definitions(full_text)

    counts = []
    for index, text in enumerate(lines):
        for call in COUNT_CALL.finditer(text):
            target = normalize_count_target(call.group(1))
            start, opener = enclosing_array(lines, index)
            entry = {
                "line": index + 1,
                "declaration": text.strip(),
                "array_line": start + 1 if start is not None else None,
                "array_kind": opener,
            }
            if target is None:
                entry["model"] = "unsupported"
                entry["reason"] = "count target is neither a subrecord signature nor a named path"
            else:
                entry["model"] = "counted"
                entry["count"] = target
            counts.append(entry)

    sorted_arrays = []
    for index, text in enumerate(lines):
        match = ARRAY_CALL.search(text.split("//", 1)[0])
        if not match:
            continue
        opener = match.group(1)
        arguments = call_arguments("\n".join(lines[index:index + ARGUMENT_WINDOW]), match.end() - 1)
        kind, keys = element_kind(element_argument(arguments), definitions)
        entry = {
            "line": index + 1,
            "array_kind": opener,
            "declaration": text.strip(),
        }
        signature = re.search(SIGNATURE, text)
        if signature:
            entry["subrecord"] = signature.group(1)
        if kind == "formid":
            entry["model"] = "formid"
        elif kind == "struct_key" and keys == [0]:
            entry["model"] = "struct_first_member"
            entry["key_index"] = 0
        elif kind == "struct_key":
            entry["model"] = "unsupported"
            entry["reason"] = (
                "sort key is member index " + ",".join(str(k) for k in keys) +
                ", whose byte offset is not derivable from the declaration")
        else:
            entry["model"] = "unsupported"
            entry["reason"] = "element is not a FormID or a first-member-keyed struct"
        sorted_arrays.append(entry)

    for path, rows in ((args.count_output, counts), (args.sorted_output, sorted_arrays)):
        document = {"version": 1, "source": Path(args.definitions).name,
                    "xedit_revision": revision, "entries": rows}
        Path(path).write_text(json.dumps(document, indent=1) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {path}: {len(rows)} entries")


if __name__ == "__main__":
    main()
