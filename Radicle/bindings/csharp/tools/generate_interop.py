#!/usr/bin/env python3
"""Generates the raw C# interop layer and a C layout probe from Radicle's public C headers.

Usage: generate_interop.py <include dir> <C# output> <C probe output>

The headers are regular (one declaration per typedef/function, C comments only), so a small
tokenizer is enough. Every public struct, enum and RADICLE_API function is emitted; anything
the mapper does not understand raises instead of being skipped.
"""
import re
import sys
from pathlib import Path

HEADERS = ["fo4/c_api.h", "fo4/c_api_env.h", "fo4/c_api_records.h", "fo4/c_api_materials.h", "fo4/c_api_maintenance.h", "fo4/c_api_navmesh.h", "fo4/c_api_sound.h", "fo4/c_api_pex.h", "fo4/c_api_material_file.h", "fo4/c_api_seq.h", "fo4/c_api_quest.h", "fo4/c_api_authoring.h", "fo4/c_api_vegetation.h", "fo4/c_api_vmad.h", "fo4/c_api_behavior.h", "fo4/c_api_dialogue.h", "fo4/c_api_loadorder.h", "fo4/c_api_conditions.h", "fo4/c_api_armor.h", "fo4/c_api_eyes.h", "fo4/c_api_faction.h", "fo4/c_api_components.h", "fo4/c_api_cell.h", "fo4/c_api_patch.h", "fo4/c_api_conflict.h", "fo4/c_api_nif.h", "fo4/c_api_dds.h", "fo4/c_api_terrain.h", "fo4/c_api_swf.h", "fo4/c_api_swf_verbs.h", "fo4/c_api_pex_analysis.h", "fo4/c_api_script_set.h", "fo4/c_api_papyrus.h", "fo4/c_api_papyrus_runtime.h", "fo4/c_api_papyrus_save.h", "fo4/c_api_papyrus_quest.h", "fo4/c_api_papyrus_lifecycle.h", "fo4/c_api_papyrus_source.h", "fo4/c_api_papyrus_compiler.h", "fo4/c_api_graph.h", "fo4/c_api_papyrus_analysis.h"]

SCALARS = {
    "int8_t": "sbyte", "uint8_t": "byte", "int16_t": "short", "uint16_t": "ushort",
    "int": "int", "int32_t": "int", "uint32_t": "uint", "int64_t": "long", "uint64_t": "ulong",
    "size_t": "nuint", "float": "float", "double": "double", "char": "byte",
}


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", "", text, flags=re.S)


def pascal(name):
    return "".join(part[:1].upper() + part[1:] for part in name.split("_") if part)


def type_name(c_name):
    return pascal(c_name[len("radicle_"):]) if c_name.startswith("radicle_") else pascal(c_name)


class Model:
    def __init__(self):
        self.enums = {}      # name -> [(member, value)]
        self.structs = {}    # name -> [(ctype, field, array_len)]
        self.opaque = set()
        self.functions = []  # (ret, name, [(ctype, pname, array_len)])
        self.order = []      # (kind, name) in header order
        self.abi_version = None


def parse(include_dir):
    model = Model()
    abi = re.search(r"^#define\s+RADICLE_ABI_VERSION\s+(\d+)u?\s*$", (include_dir / HEADERS[0]).read_text(), flags=re.M)
    if abi is None:
        raise SystemExit(f"{HEADERS[0]} does not define RADICLE_ABI_VERSION")
    model.abi_version = int(abi.group(1))
    for header in HEADERS:
        text = strip_comments((include_dir / header).read_text())
        text = re.sub(r"^\s*#.*$", "", text, flags=re.M)
        text = text.replace('extern "C" {', "")
        for match in re.finditer(r"typedef\s+enum\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, flags=re.S):
            members = []
            for item in match.group(2).split(","):
                item = item.strip()
                if not item:
                    continue
                name, value = [part.strip() for part in item.split("=")]
                members.append((name, int(value, 0)))
            model.enums[match.group(1)] = members
            model.order.append(("enum", match.group(1)))
        for match in re.finditer(r"typedef\s+struct\s+(\w+)\s+(\w+)\s*;", text):
            model.opaque.add(match.group(1))
        for match in re.finditer(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}\s*(\w+)\s*;", text, flags=re.S):
            fields = []
            for decl in match.group(2).split(";"):
                decl = " ".join(decl.split())
                if not decl:
                    continue
                pointer = re.match(r".*\(\s*\*\s*(\w+)\s*\)\s*\(.*\)$", decl)
                if pointer:
                    fields.append(("__fnptr__", pointer.group(1), None))
                else:
                    fields.append(parse_decl(decl))
            model.structs[match.group(1)] = fields
            model.order.append(("struct", match.group(1)))
        for match in re.finditer(r"RADICLE_API\s+(.*?)\s*(radicle_\w+)\s*\((.*?)\)\s*;", text, flags=re.S):
            ret = " ".join(match.group(1).replace("*", " * ").split())
            params = []
            raw = " ".join(match.group(3).split())
            if raw and raw != "void":
                params = [parse_decl(p.strip()) for p in raw.split(",")]
            model.functions.append((ret, match.group(2), params))
    return model


def parse_decl(decl):
    array = None
    arr = re.match(r"(.*?)\s*\[(\d+)\]$", decl)
    if arr:
        decl, array = arr.group(1), int(arr.group(2))
    m = re.match(r"(.*?[\s\*])(\w+)$", decl)
    if not m:
        raise SystemExit(f"cannot parse declaration: {decl!r}")
    ctype = " ".join(m.group(1).replace("*", " * ").split())
    return ctype, m.group(2), array


def base_and_depth(ctype):
    parts = [p for p in ctype.split() if p != "const"]
    depth = parts.count("*")
    base = " ".join(p for p in parts if p != "*")
    return base, depth


def field_cs(model, ctype, name, array):
    cs_name = pascal(name)
    if ctype == "__fnptr__":
        return [f"public IntPtr {cs_name};"], [(name, None)]
    base, depth = base_and_depth(ctype)
    if depth > 0:
        cs_type = "IntPtr"
    elif base in SCALARS:
        cs_type = SCALARS[base]
    elif base in model.enums:
        cs_type = type_name(base)
    elif base in model.structs:
        cs_type = type_name(base)
    else:
        raise SystemExit(f"unmapped field type {ctype} {name}")
    if array is None:
        return [f"public {cs_type} {cs_name};"], [(name, None)]
    if depth == 0 and base in SCALARS:
        return [f"public fixed {cs_type} {cs_name}[{array}];"], [(name, None)]
    return ([f"public {cs_type} {cs_name}{i};" for i in range(array)],
            [(f"{name}[{i}]", None) for i in range(array)])


def param_cs(model, ctype, name, array):
    base, depth = base_and_depth(ctype)
    ident = "@" + name if name in ("object", "string", "base", "internal", "params", "out", "ref") else name
    if array is not None:
        return f"{SCALARS[base]}* {ident}"
    if depth == 0:
        if base in SCALARS:
            return f"{SCALARS[base]} {ident}"
        if base in model.enums or base in model.structs:
            return f"{type_name(base)} {ident}"
        raise SystemExit(f"unmapped parameter {ctype} {name}")
    if base == "char" and depth == 1 and "const" in ctype:
        return f"[MarshalAs(UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(Utf8NoNulMarshaler))] string? {ident}"
    if base in model.opaque or base == "char" or base == "void":
        return "IntPtr" + "*" * (depth - 1) + f" {ident}"
    if base in SCALARS or base in model.enums or base in model.structs:
        inner = SCALARS.get(base) or type_name(base)
        return inner + "*" * depth + f" {ident}"
    raise SystemExit(f"unmapped parameter {ctype} {name}")


def ret_cs(model, ret):
    base, depth = base_and_depth(ret)
    if depth:
        return "IntPtr"
    if base == "void":
        return "void"
    if base in model.enums:
        return type_name(base)
    return SCALARS[base]


def emit_cs(model):
    out = ["// <auto-generated> by Radicle/bindings/csharp/tools/generate_interop.py from fo4/c_api*.h; edits must go through the generator.",
           "#nullable enable",
           "using System;",
           "using System.Runtime.InteropServices;",
           "",
           "namespace Radicle.Interop.Native;",
           ""]
    for kind, name in model.order:
        if kind == "enum":
            out.append(f"public enum {type_name(name)} : int")
            out.append("{")
            members = [m for m, _ in model.enums[name]]
            prefix = members[0][: members[0].rfind("_") + 1]
            while not all(m.startswith(prefix) for m in members):
                prefix = prefix[: prefix[:-1].rfind("_") + 1]
            for member, value in model.enums[name]:
                out.append(f"    {pascal(member[len(prefix):].lower())} = {value},")
            out.append("}")
            out.append("")
        else:
            out.append("[StructLayout(LayoutKind.Sequential)]")
            out.append(f"public unsafe struct {type_name(name)}")
            out.append("{")
            for ctype, field, array in model.structs[name]:
                for line in field_cs(model, ctype, field, array)[0]:
                    out.append("    " + line)
            out.append("}")
            out.append("")
    out.append("public static unsafe partial class RadicleNative")
    out.append("{")
    out.append('    public const string Library = "radicle";')
    out.append(f"    public const uint CompiledAbiVersion = {model.abi_version}u;")
    out.append("")
    for ret, name, params in model.functions:
        args = ", ".join(param_cs(model, *p) for p in params)
        out.append(f"    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, EntryPoint = \"{name}\")]")
        out.append(f"    public static extern {ret_cs(model, ret)} {pascal(name[len('radicle_'):])}({args});")
        out.append("")
    out.append("}")
    return "\n".join(out) + "\n"


def emit_probe(model):
    out = ["/* Generated by Radicle/bindings/csharp/tools/generate_interop.py from source:Radicle/include/fo4/c_api*.h; edits must go through the generator. */",
           *[f'#include "{header}"' for header in HEADERS[2:]],
           "#include <cstddef>",
           "#include <cstdio>",
           "",
           "int main() {"]
    for kind, name in model.order:
        if kind != "struct":
            continue
        cs = type_name(name)
        out.append(f'  printf("{cs} %zu\\n", sizeof({name}));')
        for ctype, field, array in model.structs[name]:
            for c_member, _ in field_cs(model, ctype, field, array)[1]:
                cs_member = pascal(field) + (c_member[c_member.index("[") + 1:-1] if "[" in c_member else "")
                out.append(f'  printf("{cs}.{cs_member} %zu\\n", offsetof({name}, {c_member}));')
    out.append("  return 0;")
    out.append("}")
    return "\n".join(out) + "\n"


def main():
    include_dir, cs_path, probe_path = map(Path, sys.argv[1:4])
    model = parse(include_dir)
    Path(cs_path).write_bytes(emit_cs(model).encode("utf-8"))
    Path(probe_path).write_bytes(emit_probe(model).encode("utf-8"))


if __name__ == "__main__":
    main()
