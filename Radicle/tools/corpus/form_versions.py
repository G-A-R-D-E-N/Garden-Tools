#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

CURRENT_FORM_VERSION = 131
OFFICIAL_MASTERS = (
    "Fallout4.esm",
    "DLCRobot.esm",
    "DLCworkshop01.esm",
    "DLCCoast.esm",
    "DLCworkshop02.esm",
    "DLCworkshop03.esm",
    "DLCNukaWorld.esm",
)
HEADER = struct.Struct("<4sIIIHHHH")
VS_SIGNATURE = struct.pack("<I", 0xFEEF04BD)
VS_STRUCT_VERSION = 0x00010000


class Census:
    def __init__(self) -> None:
        self.histogram: Counter = Counter()
        self.groups = 0

    def add(self, signature: bytes, form_version: int) -> None:
        self.histogram[(signature.decode("ascii", "replace"), form_version)] += 1


def walk(handle, start: int, end: int, census: Census) -> None:
    pos = start
    while pos < end:
        handle.seek(pos)
        raw = handle.read(HEADER.size)
        if len(raw) != HEADER.size:
            raise ValueError(f"truncated header at {pos:#x}")
        signature, size, _, _, _, _, form_version, _ = HEADER.unpack(raw)
        child_end = pos + HEADER.size + size if signature != b"GRUP" else pos + size
        if child_end > end or child_end <= pos or (signature == b"GRUP" and size < HEADER.size):
            raise ValueError(f"{signature!r} at {pos:#x} overruns its parent")
        if signature == b"GRUP":
            census.groups += 1
            walk(handle, pos + HEADER.size, child_end, census)
        else:
            census.add(signature, form_version)
        pos = child_end


def read_plugin(path: Path) -> Census:
    census = Census()
    size = path.stat().st_size
    with path.open("rb") as handle:
        raw = handle.read(HEADER.size)
        if len(raw) != HEADER.size or raw[:4] != b"TES4":
            raise ValueError("not a plugin: missing TES4 header")
        tes4 = HEADER.unpack(raw)
        census.add(b"TES4", tes4[6])
        walk(handle, HEADER.size + tes4[1], size, census)
    return census


def game_version(exe: Path) -> str | None:
    if not exe.is_file():
        return None
    data = exe.read_bytes()
    pos = data.find(VS_SIGNATURE)
    while pos >= 0:
        if pos + 16 <= len(data):
            _, struc, ms, ls = struct.unpack_from("<4I", data, pos)
            if struc == VS_STRUCT_VERSION:
                return f"{ms >> 16}.{ms & 0xFFFF}.{ls >> 16}.{ls & 0xFFFF}"
        pos = data.find(VS_SIGNATURE, pos + 1)
    return None


def listed_in_ccc(path: Path) -> set[str] | None:
    if not path.is_file():
        return None
    return {line.strip().lower() for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
            if line.strip()}


def expand(args: list[str]) -> tuple[list[Path], str | None]:
    plugins: list[Path] = []
    version = None
    for raw in args:
        target = Path(raw)
        if not target.is_dir():
            plugins.append(target)
            continue
        if version is None:
            version = game_version(target.parent / "Fallout4.exe")
        present = {p.name.lower(): p for p in target.iterdir() if p.is_file()}
        for name in OFFICIAL_MASTERS:
            if name.lower() in present:
                plugins.append(present[name.lower()])
            else:
                print(f"missing official master: {name}", file=sys.stderr)
        listed = listed_in_ccc(target.parent / "Fallout4.ccc") or None
        extras = sorted(p for lower, p in present.items()
                        if lower.startswith("cc") and lower.endswith((".esm", ".esl"))
                        and (listed is None or lower in listed))
        plugins.extend(extras)
    return plugins, version


def summarize(census: Census) -> dict:
    per_signature: dict[str, dict[int, int]] = {}
    for (signature, form_version), count in census.histogram.items():
        per_signature.setdefault(signature, {})[form_version] = count
    signatures = {}
    for signature in sorted(per_signature):
        versions = dict(sorted(per_signature[signature].items()))
        signatures[signature] = {
            "records": sum(versions.values()),
            "min": min(versions),
            "max": max(versions),
            "below_current": sum(c for v, c in versions.items() if v < CURRENT_FORM_VERSION),
            "versions": versions,
        }
    return {"records": sum(census.histogram.values()), "groups": census.groups, "signatures": signatures}


def main() -> int:
    ap = argparse.ArgumentParser(description="Read-only record form-version census (headers only, no decompression).")
    ap.add_argument("paths", nargs="+", help="plugin files, or a Data folder (the seven official masters plus cc*.esm/cc*.esl Creations)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of text")
    args = ap.parse_args()
    plugins, version = expand(args.paths)
    report = {"game_version": version, "current_form_version": CURRENT_FORM_VERSION, "plugins": []}
    failures = 0
    for path in plugins:
        try:
            summary = summarize(read_plugin(path))
        except (OSError, ValueError, struct.error) as e:
            print(f"{path.name}: {e}", file=sys.stderr)
            failures += 1
            continue
        report["plugins"].append({"name": path.name, **summary})
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(f"Fallout4.exe version: {version or 'not found'}")
        for plugin in report["plugins"]:
            print(f"\n{plugin['name']}: {plugin['records']} records, {plugin['groups']} groups")
            print(f"  {'SIG':<6}{'records':>9}{'min':>5}{'max':>5}{'<' + str(CURRENT_FORM_VERSION):>7}  versions")
            for signature, row in plugin["signatures"].items():
                versions = " ".join(f"{v}:{c}" for v, c in row["versions"].items())
                print(f"  {signature:<6}{row['records']:>9}{row['min']:>5}{row['max']:>5}{row['below_current']:>7}  {versions}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
