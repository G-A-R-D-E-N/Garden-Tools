#!/usr/bin/env python3
"""Recover the CK's NiStream SaveBinary dispatch map from PE RTTI.

The 64-bit CK has no public symbols. This tool uses MSVC x64 RTTI Complete
Object Locator records to find class vtables, reads the writer slot used by the
CK SaveStream core (+0xe0), and intersects those classes with the engine
factory inventory (CreateObject symbols from the staged NiMain exports and
Elric PDB dump).

It reports evidence rather than pretending that every RTTI class is a NIF
block: the factory intersection is the writable/loadable inventory; classes
with an inherited +0xe0 writer share the same resolved body.

Examples:
  python3 tools/extract_ck_nif_savebinary.py CreationKit.exe
  python3 tools/extract_ck_nif_savebinary.py CreationKit.exe --asm
  python3 tools/extract_ck_nif_savebinary.py CreationKit.exe --factory-symbols ghidra-dump/nimain-exports.txt ghidra-dump/elric-pdb-symbols.txt
"""
from __future__ import annotations

import argparse
import re
import struct
import subprocess
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

SAVE_SLOT = 0xE0


@dataclass(frozen=True)
class Section:
    name: str
    va: int
    raw: int
    raw_size: int
    virtual_size: int

    @property
    def end_va(self) -> int:
        return self.va + max(self.raw_size, self.virtual_size)

    @property
    def end_raw(self) -> int:
        return self.raw + self.raw_size


class PE:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"not a PE file: {path}")
        file_header = pe + 4
        section_count = struct.unpack_from("<H", self.data, file_header + 2)[0]
        opt_size = struct.unpack_from("<H", self.data, file_header + 16)[0]
        opt = file_header + 20
        if struct.unpack_from("<H", self.data, opt)[0] != 0x20B:
            raise ValueError("this extractor requires a PE32+ image")
        self.image_base = struct.unpack_from("<Q", self.data, opt + 24)[0]
        sec = opt + opt_size
        self.sections: dict[str, Section] = {}
        for i in range(section_count):
            row = sec + i * 40
            name = self.data[row:row + 8].split(b"\0", 1)[0].decode("ascii", "replace")
            virtual_size, va, raw_size, raw = struct.unpack_from("<IIII", self.data, row + 8)
            self.sections[name] = Section(name, self.image_base + va, raw, raw_size, virtual_size)

    def section(self, name: str) -> Section:
        try:
            return self.sections[name]
        except KeyError as exc:
            raise ValueError(f"missing PE section {name}") from exc

    def raw_to_va(self, raw: int) -> int | None:
        for section in self.sections.values():
            if section.raw <= raw < section.end_raw:
                return section.va + raw - section.raw
        return None

    def va_to_raw(self, va: int) -> int | None:
        for section in self.sections.values():
            if section.va <= va < section.end_va and section.raw_size:
                return section.raw + va - section.va
        return None

    def read_u64_va(self, va: int) -> int:
        raw = self.va_to_raw(va)
        if raw is None:
            raise ValueError(f"unmapped VA {va:#x}")
        return struct.unpack_from("<Q", self.data, raw)[0]

    def read_va(self, va: int, size: int) -> bytes:
        raw = self.va_to_raw(va)
        if raw is None:
            raise ValueError(f"unmapped VA {va:#x}")
        return self.data[raw:raw + size]


def factory_names(paths: list[Path]) -> set[str]:
    names: set[str] = set()
    pattern = re.compile(r"(?:^|\?)CreateObject@([^@]+(?:@[^@]+)*)@@")
    for path in paths:
        for line in path.read_text(errors="replace").splitlines():
            match = pattern.search(line)
            if match:
                names.add(match.group(1))
    return names


def get_rtti_vtables(pe: PE) -> dict[str, set[int]]:
    """Return simple MSVC class names -> all vtable starts."""
    data_sec = pe.section(".data")
    rdata = pe.section(".rdata")
    data_bytes = pe.data[data_sec.raw:data_sec.end_raw]
    descriptors: dict[int, str] = {}
    for match in re.finditer(rb"\.\?AV[^\0]{3,240}\0", data_bytes):
        decorated = match.group()[:-1].decode("ascii", "replace")
        name_va = data_sec.va + match.start()
        descriptors[name_va - 0x10] = decorated[4:-2]

    by_td_rva = {td - pe.image_base: name for td, name in descriptors.items()}
    rbytes = pe.data[rdata.raw:rdata.end_raw]
    cols: dict[int, str] = {}
    for rel in range(0, len(rbytes) - 24, 4):
        signature, _offset, _cd_offset, td_rva, chd_rva, self_rva = struct.unpack_from("<IIIIII", rbytes, rel)
        if signature not in (0, 1) or td_rva not in by_td_rva:
            continue
        col_va = rdata.va + rel
        if not (rdata.va - pe.image_base <= chd_rva < rdata.end_va - pe.image_base):
            continue
        if self_rva not in (0, col_va - pe.image_base):
            continue
        cols[col_va] = by_td_rva[td_rva]

    pointers: dict[int, list[int]] = defaultdict(list)
    for rel in range(0, len(rbytes) - 8, 8):
        value = struct.unpack_from("<Q", rbytes, rel)[0]
        if rdata.va <= value < rdata.end_va:
            pointers[value].append(rdata.va + rel)

    text = pe.section(".text")
    result: dict[str, set[int]] = defaultdict(set)
    for col_va, name in cols.items():
        for col_slot in pointers.get(col_va, []):
            vtable = col_slot + 8
            try:
                first = pe.read_u64_va(vtable)
                second = pe.read_u64_va(vtable + 8)
                writer = pe.read_u64_va(vtable + SAVE_SLOT)
            except ValueError:
                continue
            if not (text.va <= first < text.end_va and text.va <= second < text.end_va):
                continue
            if text.va <= writer < text.end_va:
                result[name].add(vtable)
    return result


def parse_pdata(pe: PE) -> list[tuple[int, int]]:
    section = pe.section(".pdata")
    result = []
    for raw in range(section.raw, section.end_raw - 11, 12):
        begin_rva, end_rva, _unwind = struct.unpack_from("<III", pe.data, raw)
        begin, end = pe.image_base + begin_rva, pe.image_base + end_rva
        text = pe.section(".text")
        if text.va <= begin < end <= text.end_va:
            result.append((begin, end))
    return sorted(result)


def function_end(entry: int, pdata: list[tuple[int, int]]) -> int | None:
    lo, hi = 0, len(pdata)
    while lo < hi:
        mid = (lo + hi) // 2
        if pdata[mid][0] <= entry:
            lo = mid + 1
        else:
            hi = mid
    if lo and pdata[lo - 1][0] <= entry < pdata[lo - 1][1]:
        return pdata[lo - 1][1]
    return None


def resolve_thunk(pe: PE, entry: int) -> int:
    text = pe.section(".text")
    seen: set[int] = set()
    current = entry
    while current not in seen and text.va <= current < text.end_va:
        seen.add(current)
        raw = pe.va_to_raw(current)
        if raw is None:
            break
        code = pe.data[raw:raw + 16]
        if code[:1] == b"\xE9":
            current += 5 + struct.unpack_from("<i", code, 1)[0]
        elif code[:2] == b"\xFF\x25":
            pointer = current + 6 + struct.unpack_from("<i", code, 2)[0]
            current = pe.read_u64_va(pointer)
        else:
            break
    return current


def recover(pe: PE, symbols: list[Path]):
    factories = factory_names(symbols)
    vtables = get_rtti_vtables(pe)
    pdata = parse_pdata(pe)
    rows = []
    for name in sorted(factories):
        for vtable in sorted(vtables.get(name, ())):
            thunk = pe.read_u64_va(vtable + SAVE_SLOT)
            entry = resolve_thunk(pe, thunk)
            rows.append((name, vtable, thunk, entry, function_end(entry, pdata)))
    return factories, rows


def print_rows(pe: PE, rows):
    print("type\tvtable\tslot_e0\tbody\tend\tbytes")
    for name, vtable, thunk, entry, end in rows:
        size = 12 if end is None else min(12, end - entry)
        data = pe.read_va(entry, size).hex()
        print(f"{name}\t{vtable:#x}\t{thunk:#x}\t{entry:#x}\t{end or 0:#x}\t{data}")


def print_asm(pe: PE, rows):
    unique = {entry: end for _name, _vtable, _thunk, entry, end in rows if end is not None}
    for entry, end in sorted(unique.items()):
        command = ["objdump", "-d", "-M", "intel", f"--start-address={entry:#x}", f"--stop-address={end:#x}", str(pe.path)]
        result = subprocess.run(command, check=True, capture_output=True, text=True)
        print(f"\n===== SaveBinary body {entry:#x}-{end:#x} =====")
        print(result.stdout, end="")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("creationkit", type=Path)
    parser.add_argument("--factory-symbols", nargs="+", type=Path, default=[
        Path("CKRE/ghidra-dump/nimain-exports.txt"),
        Path("CKRE/ghidra-dump/elric-pdb-symbols.txt"),
    ])
    parser.add_argument("--asm", action="store_true", help="also disassemble each unique .pdata-bounded body")
    args = parser.parse_args()
    pe = PE(args.creationkit)
    factories, rows = recover(pe, args.factory_symbols)
    print(f"image-base={pe.image_base:#x} factories={len(factories)} mapped={len(rows)} save-slot=+{SAVE_SLOT:#x}")
    missing = sorted(factories - {name for name, *_ in rows})
    if missing:
        print("missing-vtables=" + ",".join(missing))
    print_rows(pe, rows)
    if args.asm:
        print_asm(pe, rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
