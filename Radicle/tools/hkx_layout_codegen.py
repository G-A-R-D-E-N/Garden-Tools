#!/usr/bin/env python3
"""Generates the Havok class layout table from the measured class JSON in Radicle/data/hkx.

Member offsets, member type text, parents and instance sizes come from HavokClassLayouts.json;
class signatures, the walkable kind of each member, its written flag and its enum table come from HavokClassTypes.json.
"""
import argparse
import json
import re
import sys

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SIGNATURE = re.compile(r"^0x[0-9a-f]{8}$")

SCALARS = {
    "TYPE_BOOL": "Bool",
    "TYPE_INT8": "Int8",
    "TYPE_UINT8": "Uint8",
    "TYPE_INT16": "Int16",
    "TYPE_UINT16": "Uint16",
    "TYPE_INT32": "Int32",
    "TYPE_UINT32": "Uint32",
    "TYPE_INT64": "Int64",
    "TYPE_UINT64": "Uint64",
    "TYPE_REAL": "Real",
    "TYPE_STRINGPTR": "String",
    "TYPE_CSTRING": "String",
    "TYPE_VECTOR4": "Vector4",
    "TYPE_QUATERNION": "Quaternion",
    "TYPE_QSTRANSFORM": "QsTransform",
    "TYPE_TRANSFORM": "Transform",
    "TYPE_MATRIX3": "Matrix3",
    "TYPE_ROTATION": "Matrix3",
    "TYPE_HALF": "Half",
}
STORED = ("TYPE_ENUM", "TYPE_FLAGS")


def cpp_string(value: str) -> str:
    if not all(0x20 <= ord(ch) <= 0x7E for ch in value):
        raise ValueError(f"non-ASCII name {value!r}")
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def scalar_kind(vtype, vsub):
    return SCALARS.get(vsub if vtype in STORED else vtype)


def member_kind(info, layout_names):
    # Requires one types-file member; returns (kind, element, type_class), Other when not walkable.
    if info is None or not info.get("written") or info.get("arrsize", 0) > 0:
        return ("Other", "Other", "")
    vtype, vsub, ctype = info.get("vtype"), info.get("vsub"), info.get("ctype") or ""
    if vtype == "TYPE_STRUCT":
        return ("Struct", "Other", ctype) if ctype in layout_names else ("Other", "Other", "")
    if vtype in ("TYPE_ARRAY", "TYPE_RELARRAY"):
        kind = "Array" if vtype == "TYPE_ARRAY" else "RelArray"
        if vsub == "TYPE_STRUCT":
            return (kind, "Struct", ctype) if ctype in layout_names else ("Other", "Other", "")
        element = scalar_kind(vsub, None)
        return (kind, element, "") if element is not None else ("Other", "Other", "")
    kind = scalar_kind(vtype, vsub)
    return (kind, "Other", "") if kind is not None else ("Other", "Other", "")


def enum_index(types):
    # Requires the types file classes; returns enum name -> [(owner class, values)] over all of them.
    index = {}
    for owner, entry in types.items():
        for name, values in (entry.get("enums") or {}).items():
            index.setdefault(name, []).append((owner, values))
    return index


def resolve_enum(types, index, declaring, name):
    # Requires an enum name; the declaring class and its parents come first, then a name only one class declares.
    owner, hops = declaring, 0
    while owner in types and hops < 16:
        values = (types[owner].get("enums") or {}).get(name)
        if values is not None:
            return (owner, name, values)
        owner, hops = types[owner].get("parent"), hops + 1
    found = index.get(name, [])
    return (found[0][0], name, found[0][1]) if len(found) == 1 else None


def enum_key(resolved):
    owner, name, values = resolved
    pairs = []
    for value_name, value in values.items():
        if not IDENT.match(value_name) or not isinstance(value, int) or isinstance(value, bool):
            raise ValueError(f"enum {owner}::{name} has an entry {value_name!r}={value!r} the table cannot carry")
        pairs.append((value, value_name))
    return (owner, name, tuple(sorted(pairs)))


def collect(layouts, types):
    emitted = {name for name in layouts if name in types}
    index = enum_index(types)
    rows = []
    for name in sorted(emitted):
        if not IDENT.match(name):
            raise ValueError(f"class name {name!r} is not a C identifier")
        signature = types[name].get("signature")
        if not isinstance(signature, str) or not SIGNATURE.match(signature):
            raise ValueError(f"{name} has no signature of the form 0x + 8 lowercase hex digits")
        entry = layouts[name]
        typed = {m["name"]: m for m in types[name].get("members", [])}
        members = []
        for member in entry["members"]:
            member_name, offset = member[0], member[1]
            if not isinstance(offset, int) or offset < 0:
                raise ValueError(f"{name}.{member_name} has offset {offset!r}")
            info = typed.get(member_name)
            if info is not None and info.get("offset") != offset:
                raise ValueError(f"{name}.{member_name} has offset {offset} in the layouts and {info.get('offset')} in the types")
            text = member[2] if len(member) > 2 else ""
            written = True if info is None else bool(info.get("written", True))
            kind = member_kind(info, emitted)
            resolved = None
            if kind[0] != "Other" and info.get("vtype") in STORED and info.get("etype"):
                resolved = resolve_enum(types, index, name, info["etype"])
            members.append((member_name, offset) + kind + (text, written, enum_key(resolved) if resolved else None))
        parent = entry.get("parent")
        rows.append((name, signature, int(entry["size"]), members, parent if parent in emitted else ""))
    return rows


def render(rows) -> str:
    lines = [
        "// Generated by Radicle/tools/hkx_layout_codegen.py from Radicle/data/hkx/HavokClassLayouts.json",
        "// and Radicle/data/hkx/HavokClassTypes.json. Do not hand-edit; update the JSON source and regenerate.",
    ]
    keys = sorted({member[7] for _, _, _, members, _ in rows for member in members if member[7] is not None})
    slots = {key: at for at, key in enumerate(keys)}
    for at, (owner, name, pairs) in enumerate(keys):
        values = ", ".join("{" + cpp_string(value_name) + f", {value}" + "}" for value, value_name in pairs)
        lines.append(f"inline constexpr HkxEnumValue k_enum_values_{at}[] = {{{values}}};")
    lines.append("inline constexpr HkxEnumLayout k_enums[] = {")
    for at, (owner, name, _) in enumerate(keys):
        lines.append("  {" + f"{cpp_string(name)}, {cpp_string(owner)}, k_enum_values_{at}" + "},")
    lines.append("};")
    for name, _, _, members, _ in rows:
        if members:
            body = ", ".join(
                "{" + cpp_string(member_name) + f", {offset}u, K::{kind}, K::{element}, " + cpp_string(type_class)
                + ", " + cpp_string(text) + (", true" if written else ", false")
                + (f", &k_enums[{slots[enum]}]" if enum is not None else ", nullptr") + "}"
                for member_name, offset, kind, element, type_class, text, written, enum in members
            )
            lines.append(f"inline constexpr HkxMemberLayout k_members_{name}[] = {{{body}}};")
    lines.append("inline constexpr HkxClassLayout k_hkx_classes[] = {")
    for name, signature, size, members, parent in rows:
        table = f"k_members_{name}" if members else "{}"
        lines.append(
            "  {"
            + f"{cpp_string(name)}, {int(signature, 16)}u, {size}u, {table}, {cpp_string(parent)}"
            + "},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layouts", required=True)
    parser.add_argument("--types", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    with open(args.layouts, encoding="utf-8") as handle:
        layouts = json.load(handle)["classes"]
    with open(args.types, encoding="utf-8") as handle:
        types = json.load(handle)["classes"]
    rendered = render(collect(layouts, types))

    if args.check:
        try:
            with open(args.output, encoding="utf-8") as handle:
                current = handle.read()
        except FileNotFoundError:
            current = None
        if current != rendered:
            sys.stderr.write(
                f"{args.output} is stale; regenerate with hkx_layout_codegen.py\n")
            return 1
        return 0

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(rendered)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
