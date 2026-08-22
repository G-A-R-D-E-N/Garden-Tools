#!/usr/bin/env python3
"""Map Fallout4.exe symbol RVAs to raw offsets using the containing PE section.

The runtime symbol dumps store image RVAs, not raw file offsets.  A single
file-offset delta is invalid for unpacked Steamless images because each PE
section can have a different raw pointer.  This tool also validates the
10-byte OG CheckValidIDForSmallFile body and sweeps its recovered predicate.

Usage:
    python3 rebase_runtime_symbols.py Fallout4.exe.unpacked.exe Symbols.txt
    python3 rebase_runtime_symbols.py Fallout4.exe.unpacked.exe Symbols.txt \
        --symbol CheckValidIDForSmallFile --sweep
"""
from __future__ import annotations

import argparse
import re
import struct
from dataclasses import dataclass
from pathlib import Path

SYMBOL_RE = re.compile(
    r"func \[(?:0x)?([0-9a-fA-F]+)(?: \| sizeof=([0-9]+))?[^]]*\] (.+?)\s+::"
)
TARGET_NAME_RE = re.compile(r"^(.+?)\s+::")


@dataclass(frozen=True)
class Section:
    name: str
    va: int
    virtual_size: int
    raw_size: int
    raw_ptr: int

    @property
    def span(self) -> int:
        return max(self.virtual_size, self.raw_size)

    def contains(self, rva: int) -> bool:
        return self.va <= rva < self.va + self.span


@dataclass(frozen=True)
class Symbol:
    rva: int
    size: int | None
    name: str


def read_sections(data: bytes) -> list[Section]:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    number = struct.unpack_from("<H", data, pe + 6)[0]
    optional = pe + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    section_table = optional + (240 if magic == 0x20B else 224)
    sections = []
    for index in range(number):
        off = section_table + index * 40
        name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
        virtual_size, va, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
        sections.append(Section(name, va, virtual_size, raw_size, raw_ptr))
    return sections


def section_for(sections: list[Section], rva: int) -> Section | None:
    return next((section for section in sections if section.contains(rva)), None)


def rva_to_file_offset(sections: list[Section], rva: int) -> int | None:
    section = section_for(sections, rva)
    if section is None:
        return None
    offset = section.raw_ptr + (rva - section.va)
    if offset >= section.raw_ptr + section.raw_size:
        return None
    return offset


def parse_symbols(path: Path) -> list[Symbol]:
    symbols = []
    for line in path.read_text(errors="replace").splitlines():
        match = SYMBOL_RE.search(line)
        if match:
            qualified = match.group(3).strip()
            symbols.append(Symbol(int(match.group(1), 16),
                                  int(match.group(2)) if match.group(2) else None,
                                  qualified))
    return symbols


def check_small_file_body(body: bytes) -> bool:
    # OG 1.10.163: F7 C1 00 F0 FF 00 / 0F 94 C0 / C3
    return body.startswith(bytes.fromhex("f7 c1 00 f0 ff 00 0f 94 c0 c3"))


def small_file_id_valid(form_id: int) -> bool:
    # The body tests ECX & 0x00FFF000 and returns SETE.
    return (form_id & 0x00FFF000) == 0


def sweep_small_file() -> tuple[int, int, list[tuple[int, int]]]:
    valid = 0
    invalid = 0
    runs = []
    start = None
    previous = None
    for form_id in range(0x1000000):
        if small_file_id_valid(form_id):
            valid += 1
            if start is None:
                start = form_id
            previous = form_id
        else:
            invalid += 1
            if start is not None:
                runs.append((start, previous))
                start = previous = None
    if start is not None:
        runs.append((start, previous))
    return valid, invalid, runs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pe", type=Path)
    parser.add_argument("symbols", type=Path)
    parser.add_argument("--symbol", default="CheckValidIDForSmallFile")
    parser.add_argument("--sweep", action="store_true")
    args = parser.parse_args()

    data = args.pe.read_bytes()
    sections = read_sections(data)
    print("sections:")
    for section in sections:
        print(f"  {section.name:8} RVA 0x{section.va:08x} "
              f"vsize 0x{section.virtual_size:x} raw 0x{section.raw_ptr:x} "
              f"size 0x{section.raw_size:x} delta {section.raw_ptr - section.va:+#x}")

    symbols = parse_symbols(args.symbols)
    mapped = 0
    unmapped = 0
    for symbol in symbols:
        if rva_to_file_offset(sections, symbol.rva) is None:
            unmapped += 1
        else:
            mapped += 1
    print(f"symbols: {len(symbols)} mapped={mapped} unmapped={unmapped}")

    matches = [symbol for symbol in symbols
               if symbol.name == args.symbol or symbol.name.endswith("::" + args.symbol)]
    if not matches:
        parser.error(f"symbol not found: {args.symbol}")
    for symbol in matches:
        offset = rva_to_file_offset(sections, symbol.rva)
        body = data[offset:offset + (symbol.size or 16)] if offset is not None else b""
        file_text = f"0x{offset:x}" if offset is not None else "unmapped"
        print(f"{symbol.name}: RVA=0x{symbol.rva:x} file={file_text} "
              f"size={symbol.size} body={body[:16].hex(' ')}")
        if symbol.name == "CheckValidIDForSmallFile":
            print(f"  recovered-body={check_small_file_body(body)} "
                  "predicate=(formID & 0x00fff000) == 0")

    if args.sweep:
        valid, invalid, runs = sweep_small_file()
        print(f"small-file sweep: tested=0x1000000 valid={valid} invalid={invalid}")
        print("valid low-24-bit runs:")
        for start, end in runs[:4]:
            print(f"  0x{start:06x}-0x{end:06x}")
        if len(runs) > 4:
            print(f"  ... {len(runs) - 4} more runs")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
