#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


def read_u32(buf: memoryview, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def read_sig(buf: memoryview, off: int) -> str:
    return bytes(buf[off : off + 4]).decode("ascii", errors="replace")


@dataclass
class Subrec:
    sig: str
    size: int
    data: bytes


@dataclass
class Record:
    form_id: int
    form_version: int
    flags: int
    editor_id: str
    subrecords: list[Subrec]
    plugin: str


@dataclass
class Corpus:
    records: list[Record] = field(default_factory=list)
    order_patterns: Counter = field(default_factory=Counter)
    size_by_sig: dict[str, Counter] = field(default_factory=lambda: defaultdict(Counter))
    count_by_sig: dict[str, Counter] = field(default_factory=lambda: defaultdict(Counter))
    presence: Counter = field(default_factory=Counter)
    form_versions: Counter = field(default_factory=Counter)
    plugins: Counter = field(default_factory=Counter)


def iter_top_level_groups(buf: memoryview) -> Iterable[tuple[str, int, int]]:
    if len(buf) < 24 or read_sig(buf, 0) != "TES4":
        raise ValueError("not a plugin (missing TES4)")
    tes4_data_size = read_u32(buf, 4)
    off = 24 + tes4_data_size
    while off + 24 <= len(buf):
        sig = read_sig(buf, off)
        if sig != "GRUP":
            raise ValueError(f"expected GRUP at {off:#x}, got {sig!r}")
        group_size = read_u32(buf, off + 4)
        label = read_sig(buf, off + 8)
        group_type = read_u32(buf, off + 12)
        data_start = off + 24
        data_end = off + group_size
        if group_type == 0:
            yield label, data_start, data_end
        off = data_end


def parse_record(buf: memoryview, off: int, end: int) -> tuple[Record | None, int]:
    if off + 24 > end:
        return None, end
    sig = read_sig(buf, off)
    if sig == "GRUP":
        group_size = read_u32(buf, off + 4)
        return None, off + group_size
    data_size = read_u32(buf, off + 4)
    flags = read_u32(buf, off + 8)
    form_id = read_u32(buf, off + 12)
    form_version = struct.unpack_from("<H", buf, off + 20)[0]
    body_start = off + 24
    body_end = body_start + data_size
    if body_end > end:
        raise ValueError(f"truncated record {sig} form={form_id:#08x}")
    compressed = bool(flags & 0x00040000)
    subrecords: list[Subrec] = []
    if not compressed:
        p = body_start
        while p + 6 <= body_end:
            ssig = read_sig(buf, p)
            ssize = struct.unpack_from("<H", buf, p + 4)[0]
            p += 6
            if ssig == "XXXX" and ssize == 4 and p + 4 <= body_end:
                real_size = read_u32(buf, p)
                p += 4
                if p + 6 > body_end:
                    break
                real_sig = read_sig(buf, p)
                declared = struct.unpack_from("<H", buf, p + 4)[0]
                p += 6
                if declared != 0:
                    raise ValueError(f"XXXX follow-up size not zero for {real_sig}")
                if p + real_size > body_end:
                    raise ValueError(f"XXXX truncated {real_sig}")
                subrecords.append(Subrec(real_sig, real_size, bytes(buf[p : p + real_size])))
                p += real_size
                continue
            if p + ssize > body_end:
                raise ValueError(f"truncated subrecord {ssig} in {sig}")
            subrecords.append(Subrec(ssig, ssize, bytes(buf[p : p + ssize])))
            p += ssize
    editor_id = ""
    for sub in subrecords:
        if sub.sig == "EDID" and sub.data:
            editor_id = sub.data.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            break
    rec = Record(
        form_id=form_id,
        form_version=form_version,
        flags=flags,
        editor_id=editor_id,
        subrecords=subrecords,
        plugin="",
    )
    return rec, body_end


def collect_records(buf: memoryview, label: str) -> list[Record]:
    out: list[Record] = []
    for top_label, start, end in iter_top_level_groups(buf):
        if top_label != label:
            continue
        off = start
        while off < end:
            if off + 4 > end:
                break
            sig = read_sig(buf, off)
            if sig == "GRUP":
                group_size = read_u32(buf, off + 4)
                nested = buf[off + 24 : off + group_size]
                nested_off = 0
                nested_end = len(nested)
                while nested_off < nested_end:
                    if nested_off + 4 > nested_end:
                        break
                    nsig = read_sig(nested, nested_off)
                    if nsig == "GRUP":
                        nested_off += read_u32(nested, nested_off + 4)
                        continue
                    rec, nested_off = parse_record(nested, nested_off, nested_end)
                    if rec is not None and nsig == label:
                        out.append(rec)
                off += group_size
                continue
            if sig != label:
                data_size = read_u32(buf, off + 4)
                off = off + 24 + data_size
                continue
            rec, off = parse_record(buf, off, end)
            if rec is not None:
                out.append(rec)
    return out


def analyze_plugin(path: Path, corpus: Corpus) -> None:
    raw = path.read_bytes()
    buf = memoryview(raw)
    records = collect_records(buf, "WTHR")
    for rec in records:
        rec.plugin = path.name
        corpus.records.append(rec)
        corpus.plugins[path.name] += 1
        corpus.form_versions[rec.form_version] += 1
        order = tuple(s.sig for s in rec.subrecords)
        corpus.order_patterns[order] += 1
        counts = Counter(s.sig for s in rec.subrecords)
        for sig, n in counts.items():
            corpus.count_by_sig[sig][n] += 1
            corpus.presence[sig] += 1
        for s in rec.subrecords:
            corpus.size_by_sig[s.sig][s.size] += 1


def summarize(corpus: Corpus) -> dict:
    total = len(corpus.records)
    sigs = sorted(corpus.presence.keys())
    optional_vs_required = {}
    for sig in sigs:
        present = corpus.presence[sig]
        optional_vs_required[sig] = {
            "present_in": present,
            "absent_in": total - present,
            "required_in_corpus": present == total,
            "cardinality_histogram": dict(sorted(corpus.count_by_sig[sig].items())),
            "size_histogram": dict(sorted(corpus.size_by_sig[sig].items())),
        }

    order_rows = []
    for order, count in corpus.order_patterns.most_common():
        order_rows.append({"count": count, "order": list(order)})

    formid_candidates = {}
    known_linkish = {
        "MNAM",
        "NNAM",
        "GNAM",
        "TNAM",
        "IMSP",
        "WGDR",
        "SNAM",
        "UNAM",
        "DALC",
    }
    for sig in sigs:
        sizes = corpus.size_by_sig[sig]
        samples = []
        for rec in corpus.records:
            for s in rec.subrecords:
                if s.sig != sig:
                    continue
                if len(samples) >= 5:
                    break
                samples.append(
                    {
                        "plugin": rec.plugin,
                        "form_id": f"{rec.form_id:08X}",
                        "edid": rec.editor_id,
                        "size": s.size,
                        "head_hex": s.data[: min(64, len(s.data))].hex(),
                    }
                )
            if len(samples) >= 5:
                break
        formid_candidates[sig] = {
            "linkish_by_mutagen_name": sig in known_linkish,
            "always_4_bytes": set(sizes.keys()) == {4},
            "size_histogram": dict(sorted(sizes.items())),
            "samples": samples,
        }

    return {
        "record_count": total,
        "plugins": dict(corpus.plugins),
        "form_versions": dict(sorted(corpus.form_versions.items())),
        "signatures": optional_vs_required,
        "order_pattern_count": len(order_rows),
        "order_patterns": order_rows,
        "formid_slot_observations": formid_candidates,
        "records": [
            {
                "plugin": r.plugin,
                "form_id": f"{r.form_id:08X}",
                "form_version": r.form_version,
                "edid": r.editor_id,
                "flags": f"{r.flags:08X}",
                "subrecords": [{"sig": s.sig, "size": s.size} for s in r.subrecords],
            }
            for r in corpus.records
        ],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", type=Path)
    ap.add_argument(
        "--plugins",
        nargs="*",
        default=[
            "Fallout4.esm",
            "DLCRobot.esm",
            "DLCworkshop01.esm",
            "DLCworkshop02.esm",
            "DLCworkshop03.esm",
            "DLCCoast.esm",
            "DLCNukaWorld.esm",
        ],
    )
    ap.add_argument("--out-json", type=Path, required=True)
    ap.add_argument("--out-md", type=Path, required=True)
    args = ap.parse_args()

    corpus = Corpus()
    for name in args.plugins:
        path = args.data_dir / name
        if not path.exists():
            print(f"skip missing {path}", file=sys.stderr)
            continue
        print(f"scanning {path.name} ...", flush=True)
        analyze_plugin(path, corpus)
        print(f"  -> {corpus.plugins[path.name]} WTHR", flush=True)

    summary = summarize(corpus)
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    args.out_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    lines: list[str] = []
    lines.append("# WTHR corpus inventory (Fallout 4)")
    lines.append("")
    lines.append("Generated by `Radicle/tools/corpus/wthr_corpus_inventory.py`.")
    lines.append("Evidence only: observed on-disk layouts. Field meaning is not inferred from names.")
    lines.append("")
    lines.append(f"- Total WTHR records: **{summary['record_count']}**")
    lines.append(f"- Plugins: {', '.join(f'{k} ({v})' for k, v in summary['plugins'].items())}")
    lines.append(f"- Form versions: {summary['form_versions']}")
    lines.append(f"- Distinct subrecord order patterns: **{summary['order_pattern_count']}**")
    lines.append("")
    lines.append("## Signature presence / cardinality / sizes")
    lines.append("")
    lines.append("| Sig | Present | Absent | Required in corpus | Cardinality hist | Size hist |")
    lines.append("|-----|--------:|-------:|:-------------------:|------------------|-----------|")
    for sig, info in summary["signatures"].items():
        lines.append(
            f"| `{sig}` | {info['present_in']} | {info['absent_in']} | "
            f"{'yes' if info['required_in_corpus'] else 'no'} | "
            f"`{info['cardinality_histogram']}` | `{info['size_histogram']}` |"
        )
    lines.append("")
    lines.append("## Order patterns (most common first)")
    lines.append("")
    for i, row in enumerate(summary["order_patterns"][:20], 1):
        lines.append(f"{i}. count={row['count']}: `{' '.join(row['order'])}`")
    if len(summary["order_patterns"]) > 20:
        lines.append(f"... {len(summary['order_patterns']) - 20} more patterns in JSON")
    lines.append("")
    lines.append("## FormID-slot observations (not yet proven)")
    lines.append("")
    lines.append(
        "Mutagen/commonlib names mark some subrecords as FormLink-bearing. "
        "This table only records observed sizes/samples; prove targets/nullability separately."
    )
    lines.append("")
    for sig, info in summary["formid_slot_observations"].items():
        if not (
            info["linkish_by_mutagen_name"]
            or info["always_4_bytes"]
            or any(sz % 4 == 0 and sz <= 64 for sz in info["size_histogram"])
        ):
            continue
        lines.append(
            f"- `{sig}`: linkish_hint={info['linkish_by_mutagen_name']}, "
            f"always_4={info['always_4_bytes']}, sizes={info['size_histogram']}"
        )
    lines.append("")
    lines.append("## Per-record index")
    lines.append("")
    lines.append("| Plugin | FormID | EDID | FormVer | Subrecord sequence |")
    lines.append("|--------|--------|------|--------:|--------------------|")
    for r in summary["records"]:
        seq = " ".join(f"{s['sig']}({s['size']})" for s in r["subrecords"])
        lines.append(
            f"| {r['plugin']} | `{r['form_id']}` | `{r['edid']}` | {r['form_version']} | `{seq}` |"
        )
    lines.append("")
    args.out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
