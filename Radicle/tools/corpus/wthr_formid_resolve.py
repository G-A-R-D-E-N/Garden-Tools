#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import struct
import sys
from collections import Counter
from pathlib import Path

from wthr_corpus_inventory import Corpus, analyze_plugin, read_sig, read_u32


def build_formid_index(plugins: list[Path]) -> dict[int, tuple[str, str, str]]:
    if len(plugins) > 1:
        raise SystemExit(
            "flat FormID index mode supports one plugin at a time; "
            "multi-plugin analysis requires MAST remapping and must not emit "
            "polluted cross-plugin evidence. Use one plugin per index build."
        )
    index: dict[int, tuple[str, str, str]] = {}
    for path in plugins:
        raw = path.read_bytes()
        buf = memoryview(raw)
        if len(buf) < 24 or read_sig(buf, 0) != "TES4":
            continue
        tes4_data_size = read_u32(buf, 4)
        off = 24 + tes4_data_size
        while off + 24 <= len(buf):
            sig = read_sig(buf, off)
            if sig != "GRUP":
                break
            group_size = read_u32(buf, off + 4)
            label = read_sig(buf, off + 8)
            group_type = read_u32(buf, off + 12)
            data_start = off + 24
            data_end = off + group_size
            if group_type == 0:
                walk_group_records(buf, data_start, data_end, path.name, index)
            off = data_end
    return index


def walk_group_records(
    buf: memoryview,
    start: int,
    end: int,
    plugin: str,
    index: dict[int, tuple[str, str, str]],
) -> None:
    off = start
    while off + 24 <= end:
        sig = read_sig(buf, off)
        if sig == "GRUP":
            group_size = read_u32(buf, off + 4)
            walk_group_records(buf, off + 24, off + group_size, plugin, index)
            off += group_size
            continue
        data_size = read_u32(buf, off + 4)
        form_id = read_u32(buf, off + 12)
        flags = read_u32(buf, off + 8)
        body_start = off + 24
        body_end = body_start + data_size
        edid = ""
        compressed = bool(flags & 0x00040000)
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
                    if declared != 0 or p + real_size > body_end:
                        break
                    if real_sig == "EDID":
                        edid = bytes(buf[p : p + real_size]).split(b"\x00", 1)[0].decode(
                            "utf-8", errors="replace"
                        )
                    p += real_size
                    continue
                if p + ssize > body_end:
                    break
                if ssig == "EDID":
                    edid = bytes(buf[p : p + ssize]).split(b"\x00", 1)[0].decode(
                        "utf-8", errors="replace"
                    )
                p += ssize
        index[form_id] = (sig, plugin, edid)
        off = body_end


def resolve(index: dict[int, tuple[str, str, str]], fid: int) -> dict:
    if fid == 0:
        return {"form_id": "00000000", "null": True, "resolved": True}
    hit = index.get(fid)
    if hit is None:
        return {"form_id": f"{fid:08X}", "null": False, "resolved": False}
    sig, plugin, edid = hit
    return {
        "form_id": f"{fid:08X}",
        "null": False,
        "resolved": True,
        "target_sig": sig,
        "target_plugin": plugin,
        "target_edid": edid,
    }


def note(slot: str, store: dict, fid: int, index: dict, meta: dict) -> None:
    if slot not in store:
        store[slot] = {
            "values": 0,
            "nulls": 0,
            "unresolved": 0,
            "target_sigs": Counter(),
            "samples": [],
        }
    st = store[slot]
    st["values"] += 1
    info = resolve(index, fid)
    if info.get("null"):
        st["nulls"] += 1
    elif not info.get("resolved"):
        st["unresolved"] += 1
    else:
        st["target_sigs"][info["target_sig"]] += 1
    if len(st["samples"]) < 6:
        st["samples"].append({**meta, **info})


def analyze_slots(records, index) -> dict:
    store: dict = {}
    four_byte = {"MNAM", "NNAM", "GNAM", "TNAM"}
    for rec in records:
        meta_base = {"wthr": rec.editor_id, "plugin": rec.plugin, "form_id": f"{rec.form_id:08X}"}
        for sub in rec.subrecords:
            if sub.sig in four_byte and sub.size == 4:
                note(sub.sig, store, struct.unpack("<I", sub.data)[0], index, meta_base)
            elif sub.sig == "IMSP" and sub.size in (16, 32):
                for i in range(0, sub.size, 4):
                    fid = struct.unpack_from("<I", sub.data, i)[0]
                    note(f"IMSP+{i:02X}", store, fid, index, {**meta_base, "imsp_size": sub.size})
            elif sub.sig == "WGDR" and sub.size == 32:
                for i in range(0, 32, 4):
                    fid = struct.unpack_from("<I", sub.data, i)[0]
                    note(f"WGDR+{i:02X}", store, fid, index, meta_base)
            elif sub.sig == "SNAM" and sub.size == 8:
                fid = struct.unpack_from("<I", sub.data, 0)[0]
                typ = struct.unpack_from("<I", sub.data, 4)[0]
                note("SNAM+00", store, fid, index, {**meta_base, "snam_u32_at_4": typ})
            elif sub.sig == "UNAM" and sub.size in (12, 24):
                note("UNAM+00", store, struct.unpack_from("<I", sub.data, 0)[0], index, meta_base)
                note("UNAM+08", store, struct.unpack_from("<I", sub.data, 8)[0], index, meta_base)
    out = {}
    for k, v in sorted(store.items()):
        out[k] = {
            "values": v["values"],
            "nulls": v["nulls"],
            "unresolved": v["unresolved"],
            "target_sigs": dict(v["target_sigs"]),
            "samples": v["samples"],
        }
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("data_dir", type=Path)
    ap.add_argument("--out-json", type=Path, required=True)
    ap.add_argument("--out-md", type=Path, required=True)
    args = ap.parse_args()

    masters = [
        "Fallout4.esm",
        "DLCRobot.esm",
        "DLCworkshop01.esm",
        "DLCworkshop02.esm",
        "DLCworkshop03.esm",
        "DLCCoast.esm",
        "DLCNukaWorld.esm",
    ]
    paths = [args.data_dir / n for n in masters if (args.data_dir / n).exists()]
    if len(paths) != 1:
        raise SystemExit(
            "wthr_formid_resolve requires exactly one plugin under data_dir for flat FormID indexing; "
            "multi-plugin evidence needs MAST remapping. Pass a single-plugin data_dir or use "
            "wthr_formid_fo4_only.py for Fallout4.esm-only proof."
        )

    print("indexing FormIDs...", flush=True)
    index = build_formid_index(paths)
    print(f"indexed {len(index)} forms", flush=True)

    corpus = Corpus()
    analyze_plugin(paths[0], corpus)
    slots = analyze_slots(corpus.records, index)
    payload = {
        "scope": f"{paths[0].name} WTHR resolved against {paths[0].name} FormID index only",
        "indexed_forms": len(index),
        "wthr_records": len(corpus.records),
        "slots": slots,
    }
    args.out_json.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    lines = [
        "# WTHR FormID slot resolution evidence",
        "",
        "Observed packed `u32` values at fixed offsets inside WTHR subrecords,",
        "resolved against a single plugin master index. Null means FormID `0`.",
        "This file records resolution hits only; semantic field names require a",
        "separate runtime/layout citation (`commonlib_NonVR` `TESWeather`).",
        "",
        f"- Indexed forms: {len(index)}",
        f"- WTHR records: {len(corpus.records)}",
        "",
        "| Slot | Values | Nulls | Unresolved | Target sigs |",
        "|------|-------:|------:|-----------:|-------------|",
    ]
    for name, info in slots.items():
        lines.append(
            f"| `{name}` | {info['values']} | {info['nulls']} | {info['unresolved']} | "
            f"`{info['target_sigs']}` |"
        )
    lines.append("")
    args.out_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {args.out_json}")
    print(f"wrote {args.out_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
