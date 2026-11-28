#!/usr/bin/env python3
"""Generates the 4-byte pointer layout table of the verified Havok classes.

The class list is the decode table (Radicle/src/hkx/decode_table.cpp). Sizes and member offsets for a pointer width are
derived from HavokClassTypes.json with the BGS LayoutWalker rule (BGS/src/Hkx/PointerLayout.cs); a class whose derived
8-byte layout does not reproduce the stored size and every stored member offset is refused.
"""
import argparse
import json
import re
import sys

DECODE_CLASS = re.compile(r'^\s*\{"(\w+)", (0x[0-9a-f]{8})u, (\d+)u, k_members_\w+,$')
DECODE_ROW = re.compile(r'^\s*\{"hk')
POINTER_KINDS = ("TYPE_POINTER", "TYPE_STRINGPTR", "TYPE_CSTRING")
ALIGN_POINTER = POINTER_KINDS + ("TYPE_ULONG", "TYPE_VARIANT", "TYPE_SIMPLEARRAY", "TYPE_ARRAY")
ALIGN_16 = ("TYPE_VECTOR4", "TYPE_QUATERNION", "TYPE_QSTRANSFORM", "TYPE_MATRIX3", "TYPE_ROTATION", "TYPE_TRANSFORM", "TYPE_MATRIX4")


def storage(vt):
    if vt in ("TYPE_BOOL", "TYPE_CHAR", "TYPE_INT8", "TYPE_UINT8"):
        return 1
    if vt in ("TYPE_INT16", "TYPE_UINT16", "TYPE_HALF"):
        return 2
    if vt in ("TYPE_INT64", "TYPE_UINT64", "TYPE_ULONG") + POINTER_KINDS:
        return 8
    if vt in ("TYPE_VECTOR4", "TYPE_QUATERNION", "TYPE_ARRAY", "TYPE_SIMPLEARRAY", "TYPE_VARIANT"):
        return 16
    if vt in ("TYPE_QSTRANSFORM", "TYPE_MATRIX3", "TYPE_ROTATION"):
        return 48
    if vt in ("TYPE_TRANSFORM", "TYPE_MATRIX4"):
        return 64
    return 4


def up(value, to):
    return value if to <= 1 else (value + to - 1) // to * to


class Walker:
    def __init__(self, classes):
        self.t = classes
        self.cache = {}
        self.extra = {}

    def width(self, m, p):
        vt = m["vtype"]
        if vt in POINTER_KINDS + ("TYPE_ULONG",):
            return p
        if vt == "TYPE_RELARRAY":
            return 4
        if vt == "TYPE_VARIANT":
            return 2 * p
        if vt == "TYPE_SIMPLEARRAY":
            return p + 4
        if vt == "TYPE_ARRAY":
            return p + 8
        if vt in ("TYPE_ENUM", "TYPE_FLAGS"):
            return storage(m["vsub"])
        if vt == "TYPE_STRUCT" and m["ctype"] in self.t:
            return self.of(m["ctype"], p)[1]
        return storage(vt)

    def align(self, m, p):
        vt = m["vtype"]
        if vt in ALIGN_POINTER:
            return p
        if vt == "TYPE_RELARRAY":
            return 4
        if vt in ("TYPE_ENUM", "TYPE_FLAGS"):
            return storage(m["vsub"])
        if vt in ALIGN_16:
            return 16
        if vt == "TYPE_STRUCT":
            return self.of(m["ctype"], p)[2] if m["ctype"] in self.t else 4
        return storage(vt)

    def parent(self, c):
        p = self.t[c].get("parent")
        return p if p and p in self.t else None

    def chain(self, c):
        out = []
        seen = set()
        while c and c in self.t and c not in seen:
            seen.add(c)
            out.append(c)
            c = self.t[c].get("parent")
        members = []
        for name in reversed(out):
            members += self.t[name]["members"]
        return members

    def natural(self, c, p):
        pn = self.parent(c)
        a = self.of(pn, p)[2] if pn else (p if c == "hkBaseObject" else 1)
        for m in self.t[c]["members"]:
            a = max(a, self.align(m, p))
        return a

    def lay(self, c, p, extra):
        offsets = []
        pn = self.parent(c)
        if pn:
            parent = self.of(pn, p)
            offsets += parent[0]
            cur = parent[1]
        else:
            cur = p if c == "hkBaseObject" else 0
        declared = self.t[c]["members"]
        class_align = self.t[c].get("align") or 0
        cur = up(cur, class_align)
        for m in declared:
            cur = up(cur, self.align(m, p))
            offsets.append(cur)
            cur += self.width(m, p) * max(1, m["arrsize"])
        a = max(self.natural(c, p), extra, class_align)
        return offsets, up(cur, a), a

    def of(self, c, p):
        key = (p, c)
        if key not in self.cache:
            self.cache[key] = self.lay(c, p, self.extra_alignment(c))
        return self.cache[key]

    def extra_alignment(self, c):
        if c in self.extra:
            return self.extra[c]
        self.extra[c] = 0
        natural = self.natural(c, 8)
        found = None
        for candidate in (natural, 16, 32):
            if candidate < natural:
                continue
            offsets, size, _ = self.lay(c, 8, candidate)
            skip = len(self.chain(c)) - len(self.t[c]["members"])
            if size == self.t[c]["size"] and all(
                offsets[skip + i] == m["offset"] for i, m in enumerate(self.t[c]["members"])
            ):
                found = candidate if candidate > natural else 0
                break
        if found is None:
            raise SystemExit(f"{c}: the derived 8-byte layout does not reproduce HavokClassTypes.json")
        self.extra[c] = found
        return found


def decode_classes(path):
    with open(path, encoding="utf-8") as handle:
        lines = handle.readlines()
    rows = [DECODE_CLASS.match(line) for line in lines]
    found = [(m.group(1), int(m.group(2), 16), int(m.group(3))) for m in rows if m]
    listed = sum(1 for line in lines if DECODE_ROW.match(line))
    if listed != len(found):
        raise SystemExit(f"{path}: {listed} class rows start with {{\"hk but {len(found)} match the generator pattern; a row was reformatted")
    return found


def render(types, decode):
    walker = Walker(types)
    lines = [
        "// Constraint: generated by Radicle/tools/hkx_relayout_codegen.py from Radicle/data/hkx/HavokClassTypes.json;",
        "// Constraint: the class list is Radicle/src/hkx/decode_table.cpp, so never hand-edit, regenerate.",
        '#include "fo4/hkx/relayout_widths.hpp"',
        "",
        "namespace fo4::hkx {",
        "namespace {",
        "",
    ]
    rows = []
    for name, signature, size in decode:
        if int(types[name]["signature"], 16) != signature:
            raise SystemExit(f"{name}: decode table signature differs from HavokClassTypes.json")
        offsets8, size8, _ = walker.of(name, 8)
        offsets4, size4, align4 = walker.of(name, 4)
        if size8 != size:
            raise SystemExit(f"{name}: derived 8-byte size {size8} differs from the decode table {size}")
        pairs = [
            (offsets8[i], offsets4[i]) for i, m in enumerate(walker.chain(name)) if m["written"]
        ]
        table = ", ".join(f"{{{a}u, {b}u}}" for a, b in pairs)
        lines.append(f"constexpr HkxRelayoutOffset k_relayout_offsets_{name}[] = {{{table}}};")
        rows.append(f'    {{"{name}", {size8}u, {size4}u, k_relayout_offsets_{name}}},')
    lines.append("")
    lines.append("constexpr HkxRelayoutClass k_relayout_classes[] = {")
    lines += rows
    lines.append("};")
    lines += [
        "",
        "}  // namespace",
        "",
        "std::span<const HkxRelayoutClass> relayout_classes() { return k_relayout_classes; }",
        "",
        "}  // namespace fo4::hkx",
    ]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--types", required=True)
    parser.add_argument("--decode-table", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    with open(args.types, encoding="utf-8") as handle:
        types = json.load(handle)["classes"]
    rendered = render(types, decode_classes(args.decode_table))
    if args.check:
        try:
            with open(args.output, encoding="utf-8") as handle:
                current = handle.read()
        except FileNotFoundError:
            current = None
        if current != rendered:
            sys.stderr.write(f"{args.output} is stale; regenerate with hkx_relayout_codegen.py\n")
            return 1
        return 0
    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
