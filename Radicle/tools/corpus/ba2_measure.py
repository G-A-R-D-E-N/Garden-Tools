#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

SENTINEL = 0xBAADF00D
LAYOUT = {b"GNRL": (0, 0x10, "<QIII"), b"DX10": (8, 0x18, "<QIIHHI")}


def measure(path: Path, first_chunk: bool) -> tuple[int, str, int, int, list[str], bool | None]:
    problems: list[str] = []
    version, tag, count, chunks, decoded = 0, b"", 0, 0, None
    try:
        size = path.stat().st_size
        with path.open("rb") as f:
            magic, version, tag, count, names = struct.unpack("<4sI4sIQ", f.read(24))
            if magic != b"BTDX" or version not in (1, 7, 8) or tag not in LAYOUT:
                return version, tag.decode("ascii", "replace"), count, 0, ["unsupported header"], None
            extra, header_field, chunk_fmt = LAYOUT[tag]
            first = None
            for _ in range(count):
                _, _, _, _, n, hdr = struct.unpack("<IIIBBH", f.read(16))
                f.read(extra)
                if hdr != header_field:
                    problems.append(f"chunk-header-size {hdr:#x}")
                for _ in range(n):
                    fields = struct.unpack(chunk_fmt, f.read(struct.calcsize(chunk_fmt)))
                    offset, packed, unpacked, sentinel = fields[0], fields[1], fields[2], fields[-1]
                    if sentinel != SENTINEL:
                        problems.append(f"sentinel {sentinel:#x}")
                    if offset + (packed or unpacked) > size:
                        problems.append("payload out of bounds")
                    if first is None:
                        first = (offset, packed, unpacked)
                    chunks += 1
            if names > size:
                problems.append("name table past end of file")
            if names and f.tell() > names:
                problems.append("table overlaps name table")
            if first_chunk and first is not None:
                offset, packed, unpacked = first
                f.seek(offset)
                data = f.read(packed or unpacked)
                decoded = len(zlib.decompress(data) if packed else data) == unpacked
    except (struct.error, zlib.error, ValueError, OverflowError, OSError) as e:
        problems.append(f"malformed archive: {e}")
    return version, tag.decode("ascii", "replace"), count, chunks, problems, decoded


def exe_file_version(path: Path) -> str | None:
    if not path.is_file():
        return None
    data = path.read_bytes()
    at = data.find(struct.pack("<I", 0xFEEF04BD))
    if at < 0 or at + 16 > len(data):
        return None
    ms, ls = struct.unpack_from("<II", data, at + 8)
    return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Read-only BA2 header and table measurement.")
    ap.add_argument("data_dir", nargs="?", default=os.environ.get("FO4_DATA_DIR"))
    ap.add_argument("--first-chunk", action="store_true", help="also decode each archive's first chunk")
    ap.add_argument("--json", action="store_true", help="print JSON instead of the Markdown table")
    args = ap.parse_args()
    if not args.data_dir:
        ap.error("pass a Data folder or set FO4_DATA_DIR")
    try:
        archives = sorted(p for p in Path(args.data_dir).iterdir() if p.suffix.lower() == ".ba2")
    except OSError as e:
        print(f"{args.data_dir}: {e.strerror or e}", file=sys.stderr)
        return 1
    totals: Counter = Counter()
    failures = 0
    for path in archives:
        version, tag, count, chunks, problems, decoded = measure(path, args.first_chunk)
        if problems or decoded is False:
            failures += 1
            print(f"{path.name}: {sorted(set(problems))} first-chunk={decoded}", file=sys.stderr)
            continue
        totals[(version, tag, "archives")] += 1
        totals[(version, tag, "entries")] += count
        totals[(version, tag, "chunks")] += chunks
        if decoded is not None:
            totals[(version, tag, "first-chunk ok")] += decoded
    rows = []
    for version, tag in sorted({k[:2] for k in totals}, key=lambda k: (k[0], k[1] != "GNRL")):
        row = {"version": version, "content": tag}
        for name in ("archives", "entries", "chunks"):
            row[name] = totals[(version, tag, name)]
        if args.first_chunk:
            row["first_chunk_ok"] = totals[(version, tag, "first-chunk ok")]
        rows.append(row)
    if args.json:
        exe = Path(args.data_dir).resolve().parent / "Fallout4.exe"
        report = {
            "data_dir": str(args.data_dir),
            "fallout4_exe_version": exe_file_version(exe),
            "rows": rows,
            "archives": len(archives),
            "archives_with_problems": failures,
        }
        print(json.dumps(report, indent=2))
        return 1 if failures else 0
    print("| Version | Content | Archives | Entries | Chunks |" + (" First chunk ok |" if args.first_chunk else ""))
    print("|---|---|---:|---:|---:|" + ("---:|" if args.first_chunk else ""))
    for row in rows:
        values = [row["archives"], row["entries"], row["chunks"]]
        if args.first_chunk:
            values.append(row["first_chunk_ok"])
        print(f"| {row['version']} | {row['content']} | " + " | ".join(map(str, values)) + " |")
    print(f"{len(archives)} archives, {failures} with problems")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
