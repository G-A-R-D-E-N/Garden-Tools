#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from wthr_corpus_inventory import Corpus, analyze_plugin
from wthr_formid_resolve import analyze_slots, build_formid_index


def resolve_data_dir(args: argparse.Namespace) -> Path:
    if args.data_dir is not None:
        return args.data_dir
    env = os.environ.get("FO4_DATA_DIR")
    if env:
        return Path(env)
    raise SystemExit("missing data directory: pass --data-dir or set FO4_DATA_DIR")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Fallout 4 Data folder (required unless FO4_DATA_DIR is set)",
    )
    args = ap.parse_args()
    data = resolve_data_dir(args)
    if not data.is_dir():
        raise SystemExit(f"data directory not found: {data}")

    fo4 = data / "Fallout4.esm"
    if not fo4.is_file():
        raise SystemExit(f"Fallout4.esm not found under {data}")

    index = build_formid_index([fo4])
    corpus = Corpus()
    analyze_plugin(fo4, corpus)
    slots = analyze_slots(corpus.records, index)
    docs = Path(__file__).resolve().parents[2] / "docs"
    payload = {
        "scope": "Fallout4.esm WTHR resolved against Fallout4.esm FormID index only",
        "indexed_forms": len(index),
        "wthr_records": len(corpus.records),
        "slots": slots,
        "note": (
            "Multi-plugin flat FormID indexes collide across DLC master indexes; "
            "use load-order remapping for DLC proof."
        ),
    }
    (docs / "wthr_formid_slots_fo4.json").write_text(json.dumps(payload, indent=2), encoding="utf-8")
    lines = [
        "# WTHR FormID slot resolution evidence (Fallout4.esm)",
        "",
        "Scope: WTHR records in Fallout4.esm resolved against the Fallout4.esm FormID index only.",
        "A flat multi-DLC FormID index collides on overlapping master indexes; those polluted hits are not used as proof.",
        "",
        f"- Indexed forms: {len(index)}",
        f"- WTHR records: {len(corpus.records)}",
        "",
        "| Slot | Values | Nulls | Unresolved | Target sigs |",
        "|------|-------:|------:|-----------:|-------------|",
    ]
    for name, info in sorted(slots.items()):
        lines.append(
            f"| `{name}` | {info['values']} | {info['nulls']} | {info['unresolved']} | "
            f"`{info['target_sigs']}` |"
        )
    lines.extend(
        [
            "",
            "## Runtime member citation (commonlib_NonVR `TESWeather`)",
            "",
            "Reference-only: `commonlib_NonVR/include/RE/T/TESWeather.h`.",
            "",
            "| On-disk slot | Runtime member | Pointer/element type |",
            "|--------------|----------------|----------------------|",
            "| `MNAM` | `precipitationData` | `BGSShaderParticleGeometryData*` (SPGD) |",
            "| `NNAM` | `referenceEffect` | `BGSReferenceEffect*` (RFCT) |",
            "| `IMSP` (4 or 8 x u32) | `imageSpace[8]` | `TESImageSpace*` (IMGS) |",
            "| `WGDR` (8 x u32) | `godRays[8]` | `BGSGodRays*` (GDRY) |",
            "| `GNAM` | `sunGlareLensFlare` | `BGSLensFlare*` (LENS) |",
            "| `TNAM` repeated | `skyStatics` | `TESObjectSTAT*` (STAT) |",
            "| `SNAM` `{u32,u32}` | `soundList` / `WeatherSound` | `BGSSoundDescriptorForm*` + `SoundType` |",
            "| `UNAM` | `weatherSpellData[2]` | `MagicItem*` + float threshold (SPEL on disk) |",
            "",
            "`DALC` is not a FormID container: each 32-byte `DALC` matches "
            "`sizeof(BGSDirectionalAmbientLightingColors)==0x20`.",
            "`VNAM`/`WNAM` are floats (`volatilityMult`/`visibilityMult`), not FormIDs.",
            "`NAM1` is `cloudLayerDisabledBits` (u32 bitfield), not a FormID.",
            "`LNAM` remains opaque (always 4 bytes in corpus; no proven semantic mapping yet).",
            "",
        ]
    )
    (docs / "WTHR_FORMID_SLOTS.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("updated", docs / "WTHR_FORMID_SLOTS.md")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
