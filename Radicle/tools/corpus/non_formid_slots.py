"""Usage: non_formid_slots.py <fo4inspect> <Fallout 4 Data dir> <wbDefinitionsFO4.pas> <output .inc> [wbDefinitionsCommon.pas]

fo4inspect must be built with an empty non_formid_slots.inc so the scan sees every candidate slot.
"""

import collections
import json
import pathlib
import re
import subprocess
import sys

OFFICIAL = re.compile(r"^(Fallout4\.esm|DLC[A-Za-z0-9]+\.esm|cc[A-Z]{3}FO4\d{3}-[A-Za-z0-9_]+\.esl)$")
SCALAR_TYPES = {"wbFloat", "wbInteger", "wbString", "wbLString", "wbRGBA", "wbByteRGBA", "wbFloatRGBA", "wbByteArray",
                "wbByteColors", "wbByteABGR", "wbFloatColors", "wbLStringKC", "wbStringKC",
                "wbLenString", "wbStringForward", "wbUnknown"}
XEDIT_DENIED = [
    ("AACT", "DNAM"),  # xEdit wbDefinitionsFO4.pas:6976 wbString
    ("ACHR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:4623 wbString
    ("AMMO", "NAM1"),  # xEdit wbDefinitionsFO4.pas:5773 wbString
    ("ANIO", "BNAM"),  # xEdit wbDefinitionsFO4.pas:5783 wbString
    ("BOOK", "CNAM"),  # xEdit wbDefinitionsFO4.pas:5929 wbLStringKC
    ("BPTD", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7776 wbString
    ("CLMT", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6122 wbString
    ("CLMT", "GNAM"),  # xEdit wbDefinitionsFO4.pas:6123 wbString
    ("COLL", "MNAM"),  # xEdit wbDefinitionsFO4.pas:9557 wbString
    ("DOOR", "CNAM"),  # xEdit wbDefinitionsFO4.pas:6506 wbLStringKC
    ("EFSH", "NAM7"),  # xEdit wbDefinitionsFO4.pas:6514 wbString
    ("EFSH", "NAM8"),  # xEdit wbDefinitionsFO4.pas:6515 wbString
    ("EYES", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6775 wbLString
    ("EYES", "INAM"),  # xEdit wbDefinitionsFO4.pas:6776 wbString
    ("EYES", "MNAM"),  # xEdit wbDefinitionsFO4.pas:6774 wbLString
    ("FACT", "RNAM"),  # xEdit wbDefinitionsFO4.pas:6773 wbFactionRank Rank# wbInteger(RNAM), used by FACT:6830
    ("FLOR", "RNAM"),  # xEdit wbDefinitionsFO4.pas:12179 wbLStringKC(RNAM) in FLOR:12167
    ("FSTP", "ANAM"),  # xEdit wbDefinitionsFO4.pas:8689 wbString
    ("HDPT", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7049 wbString
    ("IDLE", "DNAM"),  # xEdit wbDefinitionsFO4.pas:9649 wbString
    ("IDLE", "GNAM"),  # xEdit wbDefinitionsFO4.pas:9669 wbString
    ("KYWD", "DNAM"),  # xEdit wbDefinitionsFO4.pas:6953 wbString
    ("KYWD", "NNAM"),  # xEdit wbDefinitionsFO4.pas:6959 wbString
    ("LENS", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12413 wbString
    ("LIGH", "NAM0"),  # xEdit wbDefinitionsFO4.pas:9940 wbString
    ("MATT", "ANAM"),  # xEdit wbDefinitionsFO4.pas:7974 wbString
    ("MESG", "SNAM"),  # xEdit wbDefinitionsFO4.pas:8197 wbString
    ("MSWP", "BNAM"),  # xEdit wbDefinitionsFO4.pas:12445 wbString
    ("MSWP", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12442 wbString
    ("MSWP", "SNAM"),  # xEdit wbDefinitionsFO4.pas:12446 wbString
    ("MUST", "ANAM"),  # xEdit wbDefinitionsFO4.pas:8806 wbString
    ("MUST", "BNAM"),  # xEdit wbDefinitionsFO4.pas:8807 wbString
    ("NOCM", "NAM1"),  # xEdit wbDefinitionsFO4.pas:12466 wbString
    ("NOTE", "PNAM"),  # xEdit wbDefinitionsFO4.pas:12498 wbString
    ("PARW", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PBAR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PBEA", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PCON", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PERK", "FNAM"),  # xEdit wbDefinitionsFO4.pas:7605 wbString
    ("PFLA", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PGRE", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PHZD", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PMIS", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PROJ", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7173 wbString
    ("RACE", "ANAM"),  # xEdit wbDefinitionsFO4.pas:11196 wbString
    ("RACE", "HNAM"),  # xEdit wbDefinitionsFO4.pas:11340 wbString
    ("RACE", "NAME"),  # xEdit wbDefinitionsFO4.pas:11248 wbString
    ("RACE", "ZNAM"),  # xEdit wbDefinitionsFO4.pas:11033 wbString(wbEquipSlot)
    ("REFR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:11865 wbString
    ("SCEN", "NNAM"),  # xEdit wbDefinitionsFO4.pas:9130 wbString
    ("SNDR", "ANAM"),  # xEdit wbDefinitionsFO4.pas wbString, GT-173 (line not pinned)
    ("SNDR", "FNAM"),  # xEdit wbDefinitionsFO4.pas:9446 wbString
    ("SNDR", "NNAM"),  # xEdit wbDefinitionsFO4.pas:9408 wbString
    ("SPGD", "MNAM"),  # xEdit wbDefinitionsFO4.pas:6160 wbString
    ("TERM", "VNAM"),  # xEdit wbDefinitionsFO4.pas:12752 wbString
    ("TES4", "CNAM"),  # xEdit wbDefinitionsFO4.pas:12102 wbString
    ("TES4", "SNAM"),  # xEdit wbDefinitionsFO4.pas:12103 wbString
    ("TXST", "MNAM"),  # xEdit wbDefinitionsFO4.pas:7003 wbString
    ("WATR", "NAM2"),  # xEdit wbDefinitionsFO4.pas:12872 wbString
    ("WATR", "NAM3"),  # xEdit wbDefinitionsFO4.pas:12873 wbString
    ("WATR", "NAM4"),  # xEdit wbDefinitionsFO4.pas:12874 wbString
    ("WRLD", "NNAM"),  # xEdit wbDefinitionsFO4.pas:13170 wbString
    ("WRLD", "TNAM"),  # xEdit wbDefinitionsFO4.pas:13172 wbString
]


def inspect(tool, command, path):
    out = subprocess.run([tool, command, str(path)], check=True, capture_output=True).stdout
    return json.loads(out.decode("utf-8"))


def bracket_span(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] in "([":
            depth += 1
        elif text[i] in ")]":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
    return text[start:]


def record_blocks(defs):
    starts = [(m.start(), m.group(1)) for m in re.finditer(r"wbRecord\((\w{4}),", defs)]
    blocks = collections.defaultdict(str)
    for i, (pos, sig) in enumerate(starts):
        end = starts[i + 1][0] if i + 1 < len(starts) else len(defs)
        blocks[sig] += defs[pos:end]
    return blocks


STRUCTURAL = {"wbStruct", "wbRStruct", "wbArray", "wbArrayS", "wbRArray", "wbEnum", "wbFlags", "wbEmpty"}


HELPERS = {}


def helper_body(defs, name):
    if name not in HELPERS:
        m = (re.search(r"\b" + re.escape(name) + r"\s*:=", defs) or
             re.search(r"\bfunction\s+" + re.escape(name) + r"\s*:\s*\w+\s*;\s*begin\s*Result\s*:=", defs))
        HELPERS[name] = None
        if m and "(" in defs[m.end():m.end() + 200]:
            paren = defs.index("(", m.end())
            HELPERS[name] = defs[m.end():paren] + bracket_span(defs, paren)
    return HELPERS[name]


def span_is_scalar(defs, span, depth=0):
    if depth > 8 or "wbFormID" in span or "wbUnion" in span:
        return False
    for ident in set(re.findall(r"\bwb\w+", span)) - SCALAR_TYPES - STRUCTURAL:
        body = helper_body(defs, ident)
        if body is not None and not span_is_scalar(defs, body, depth + 1):
            return False
    return True


def direct_verdicts(defs, text, subrecord):
    verdicts = []
    for m in re.finditer(r"(wb\w+)\(\s*" + subrecord + r"\s*[,)]", text):
        kind = m.group(1)
        if kind in SCALAR_TYPES:
            verdicts.append(True)
        elif kind in {"wbStruct", "wbUnion", "wbArray", "wbArrayS"}:
            verdicts.append(span_is_scalar(defs, bracket_span(text, m.end(1))))
        else:
            verdicts.append(False)
    return verdicts


def slot_is_scalar(defs, block, subrecord):
    """True only when every definition of `subrecord` in the record is provably not a form ID."""
    verdicts = direct_verdicts(defs, block, subrecord)
    if not verdicts:
        for helper in sorted(set(re.findall(r"\b(wb\w+)\b", block)) - SCALAR_TYPES - STRUCTURAL):
            body = helper_body(defs, helper)
            if body is not None:
                verdicts.extend(direct_verdicts(defs, body, subrecord))
    return bool(verdicts) and all(verdicts)


def main():
    tool, data_dir, defs_path, output = sys.argv[1], pathlib.Path(sys.argv[2]), sys.argv[3], pathlib.Path(sys.argv[4])
    defs = "\n".join(open(path, encoding="utf-8", errors="replace").read() for path in [defs_path, *sys.argv[5:]])
    defs = re.sub(r"\{[^{}$]*\}", "", defs)
    defs = re.sub(r"//[^\n]*", "", defs)
    blocks = record_blocks(defs)
    files = sorted(p for p in data_dir.iterdir() if OFFICIAL.match(p.name))
    records = {path: inspect(tool, "records", path)["records"] for path in files}
    known = {r["form_key"] for rows in records.values() for r in rows}
    seen = collections.Counter()
    for path in files:
        masters = len(inspect(tool, "masters", path)["masters"])
        signatures = {r["form_key"]: r["signature"] for r in records[path]}
        for hit in inspect(tool, "formlinks", path)["form_links"]:
            raw = int(hit["raw"], 16)
            if raw != 0 and (raw >> 24 > masters or hit["form_key"] not in known):
                seen[(signatures.get(hit["record"], "????"), hit["subrecord"])] += 1

    denied, kept = {}, []
    for pair, count in sorted(seen.items()):
        if slot_is_scalar(defs, blocks.get(pair[0], ""), pair[1]):
            denied[pair] = count
        else:
            kept.append((pair, count))
    denied.update({pair: "xEdit" for pair in XEDIT_DENIED})

    lines = [
        "// Generated by Radicle/tools/corpus/non_formid_slots.py: slots where the official masters store",
        "// values that resolve to no official record AND xEdit wbDefinitionsFO4.pas declares the subrecord",
        "// in that record only as non-form-ID data. Entries marked xEdit come from XEDIT_DENIED, not the corpus.",
        "// Corpus: " + ", ".join(p.name for p in files),
    ]
    for (record, subrecord), count in sorted(denied.items()):
        lines.append(f'{{Signature::from_string("{record}"), Signature::from_string("{subrecord}")}},  // {count}')
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"denied {len(denied)}, kept {len(kept)} from {len(files)} files", file=sys.stderr)
    for (record, subrecord), count in kept:
        print(f"kept {record} {subrecord} ({count})", file=sys.stderr)


if __name__ == "__main__":
    main()
