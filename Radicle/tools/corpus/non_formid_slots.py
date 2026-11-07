"""Usage: non_formid_slots.py <fo4inspect> <Fallout 4 Data dir> <wbDefinitionsFO4.pas> <output .inc> [wbDefinitionsCommon.pas]
       non_formid_slots.py --check-static <wbDefinitionsFO4.pas> <formid_subrecords.inc> <non_formid_slots.inc> [<manual.inc>] [wbDefinitionsCommon.pas]

fo4inspect must be built with an empty non_formid_slots.inc so the scan sees every candidate slot.
"""

import collections
import json
import pathlib
import re
import subprocess
import sys

XEDIT_ONLY_SCALAR_SLOTS = [("FSTP", "ANAM"), ("SNDR", "ANAM"), ("SNDR", "FNAM"), ("SNDR", "NNAM"),
                           ("MUST", "ANAM"), ("MUST", "BNAM"), ("MUST", "FNAM"), ("SOPM", "MNAM")]
OFFICIAL = re.compile(r"^(Fallout4\.esm|DLC[A-Za-z0-9]+\.esm|cc[A-Z]{3}FO4\d{3}-[A-Za-z0-9_]+\.esl)$")
SCALAR_TYPES = {"wbFloat", "wbInteger", "wbString", "wbLString", "wbRGBA", "wbByteRGBA", "wbFloatRGBA", "wbByteArray",
                "wbByteColors", "wbByteABGR", "wbFloatColors", "wbLStringKC", "wbStringKC",
                "wbLenString", "wbStringForward", "wbUnknown"}
XEDIT_DENIED = [
    ("AACT", "DNAM"),  # xEdit wbDefinitionsFO4.pas:6976 wbString
    ("AACT", "TNAM"),  # xEdit wbDefinitionsFO4.pas:6977 wbInteger
    ("ACHR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:4623 wbString
    ("ACTI", "FNAM"),  # xEdit wbDefinitionsFO4.pas:5652 wbInteger
    ("AMDL", "DNAM"),  # xEdit wbDefinitionsFO4.pas:12248 wbStruct
    ("AMMO", "NAM1"),  # xEdit wbDefinitionsFO4.pas:5773 wbString
    ("ANIO", "BNAM"),  # xEdit wbDefinitionsFO4.pas:5783 wbString
    ("ARMA", "DNAM"),  # xEdit wbDefinitionsFO4.pas:5854 wbStruct
    ("ARMO", "FNAM"),  # xEdit wbDefinitionsFO4.pas:5833 wbStruct
    ("ARTO", "DNAM"),  # xEdit wbDefinitionsFO4.pas:9368 wbInteger
    ("ASPC", "WNAM"),  # xEdit wbDefinitionsFO4.pas:7064 wbInteger
    ("BNDS", "DNAM"),  # xEdit wbDefinitionsFO4.pas:12283 wbStruct
    ("BOOK", "CNAM"),  # xEdit wbDefinitionsFO4.pas:5929 wbLStringKC
    ("BPTD", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7776 wbString
    ("CELL", "CNAM"),  # xEdit wbDefinitionsFO4.pas:6038 wbInteger
    ("CELL", "ZNAM"),  # xEdit wbDefinitionsFO4.pas:6039 wbInteger
    ("CLFM", "FNAM"),  # xEdit wbDefinitionsFO4.pas:9585 wbInteger
    ("CLMT", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6122 wbString
    ("CLMT", "GNAM"),  # xEdit wbDefinitionsFO4.pas:6123 wbString
    ("COLL", "GNAM"),  # xEdit wbDefinitionsFO4.pas:9552 wbInteger
    ("COLL", "MNAM"),  # xEdit wbDefinitionsFO4.pas:9557 wbString
    ("CSTY", "CSCR"),  # xEdit wbDefinitionsFO4.pas:6267 wbStruct
    ("DLBR", "DNAM"),  # xEdit wbDefinitionsFO4.pas:8786 wbInteger
    ("DLBR", "TNAM"),  # xEdit wbDefinitionsFO4.pas:8781 wbInteger
    ("DOOR", "CNAM"),  # xEdit wbDefinitionsFO4.pas:6506 wbLStringKC
    ("DOOR", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6496 wbInteger
    ("EFSH", "NAM7"),  # xEdit wbDefinitionsFO4.pas:6514 wbString
    ("EFSH", "NAM8"),  # xEdit wbDefinitionsFO4.pas:6515 wbString
    ("EYES", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6775 wbLString
    ("EYES", "INAM"),  # xEdit wbDefinitionsFO4.pas:6776 wbString
    ("EYES", "MNAM"),  # xEdit wbDefinitionsFO4.pas:6774 wbLString
    ("FACT", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6775 wbLString
    ("FACT", "INAM"),  # xEdit wbDefinitionsFO4.pas:6776 wbString
    ("FACT", "MNAM"),  # xEdit wbDefinitionsFO4.pas:6774 wbLString
    ("FACT", "RNAM"),  # xEdit wbDefinitionsFO4.pas:6773 wbFactionRank Rank# wbInteger(RNAM), used by FACT:6830
    ("FLOR", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12180 wbInteger
    ("FLOR", "RNAM"),  # xEdit wbDefinitionsFO4.pas:12179 wbLStringKC(RNAM) in FLOR:12167
    ("FSTP", "ANAM"),  # xEdit wbDefinitionsFO4.pas:8689 wbString
    ("FURN", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6875 wbInteger
    ("FURN", "NAM0"),  # xEdit wbDefinitionsFO4.pas:6904 wbStruct
    ("GLOB", "FNAM"),  # xEdit wbDefinitionsFO4.pas:6926 wbInteger
    ("HDPT", "NAM0"),  # xEdit wbDefinitionsFO4.pas:7043 wbInteger
    ("HDPT", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7049 wbString
    ("IDLE", "DNAM"),  # xEdit wbDefinitionsFO4.pas:9649 wbString
    ("IDLE", "GNAM"),  # xEdit wbDefinitionsFO4.pas:9669 wbString
    ("IMAD", "DNAM"),  # xEdit wbDefinitionsFO4.pas:7468 wbStruct
    ("IMAD", "NAM3"),  # xEdit wbDefinitionsFO4.pas:7531 wbArray
    ("IMAD", "TNAM"),  # xEdit wbDefinitionsFO4.pas:7530 wbArray
    ("IMGS", "CNAM"),  # xEdit wbDefinitionsFO4.pas:7426 wbStruct
    ("IMGS", "DNAM"),  # xEdit wbDefinitionsFO4.pas:7435 wbStruct
    ("IMGS", "ENAM"),  # xEdit wbDefinitionsFO4.pas:7395 wbStruct
    ("IMGS", "HNAM"),  # xEdit wbDefinitionsFO4.pas:7415 wbStruct
    ("IMGS", "TNAM"),  # xEdit wbDefinitionsFO4.pas:7431 wbStruct
    ("INFO", "INAM"),  # xEdit wbDefinitionsFO4.pas:9779 wbInteger
    ("INFO", "NAM0"),  # xEdit wbDefinitionsFO4.pas:9766 wbString
    ("INFO", "NAM1"),  # xEdit wbDefinitionsFO4.pas:9741 wbLStringKC
    ("INFO", "NAM2"),  # xEdit wbDefinitionsFO4.pas:9744 wbString
    ("INFO", "NAM3"),  # xEdit wbDefinitionsFO4.pas:9745 wbString
    ("INFO", "NAM4"),  # xEdit wbDefinitionsFO4.pas:9746 wbString
    ("INFO", "RNAM"),  # xEdit wbDefinitionsFO4.pas:9755 wbLStringKC
    ("INFO", "TNAM"),  # xEdit wbDefinitionsFO4.pas:9749 wbInteger
    ("INNR", "XNAM"),  # xEdit wbDefinitionsFO4.pas:12354 wbStruct
    ("INNR", "YNAM"),  # xEdit wbDefinitionsFO4.pas:12380 wbInteger
    ("KYWD", "DNAM"),  # xEdit wbDefinitionsFO4.pas:6953 wbString
    ("KYWD", "NNAM"),  # xEdit wbDefinitionsFO4.pas:6959 wbString
    ("LCRT", "TNAM"),  # xEdit wbDefinitionsFO4.pas:6965 wbInteger
    ("LCTN", "CNAM"),  # xEdit wbDefinitionsFO4.pas:8176 wbByteRGBA
    ("LENS", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12413 wbString
    ("LIGH", "NAM0"),  # xEdit wbDefinitionsFO4.pas:9940 wbString
    ("LSCR", "ZNAM"),  # xEdit wbDefinitionsFO4.pas:9960 wbStruct
    ("LTEX", "HNAM"),  # xEdit wbDefinitionsFO4.pas:9971 wbStruct
    ("LTEX", "SNAM"),  # xEdit wbDefinitionsFO4.pas:9975 wbInteger
    ("MATO", "DNAM"),  # xEdit wbDefinitionsFO4.pas:9379 wbByteArray
    ("MATT", "ANAM"),  # xEdit wbDefinitionsFO4.pas:7974 wbString
    ("MATT", "CNAM"),  # xEdit wbDefinitionsFO4.pas:7962 wbFloatColors
    ("MATT", "FNAM"),  # xEdit wbDefinitionsFO4.pas:7967 wbInteger
    ("MESG", "DNAM"),  # xEdit wbDefinitionsFO4.pas:8185 wbInteger
    ("MESG", "SNAM"),  # xEdit wbDefinitionsFO4.pas:8197 wbString
    ("MOVT", "INAM"),  # xEdit wbDefinitionsFO4.pas:9397 wbStruct
    ("MSWP", "BNAM"),  # xEdit wbDefinitionsFO4.pas:12445 wbString
    ("MSWP", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12442 wbString
    ("MSWP", "SNAM"),  # xEdit wbDefinitionsFO4.pas:12446 wbString
    ("MUST", "ANAM"),  # xEdit wbDefinitionsFO4.pas:8806 wbString
    ("MUST", "BNAM"),  # xEdit wbDefinitionsFO4.pas:8807 wbString
    ("MUST", "LNAM"),  # xEdit wbDefinitionsFO4.pas:8808 wbStruct
    ("NAVM", "NNAM"),  # xEdit wbDefinitionsFO4.pas:7225 wbArray
    ("NAVM", "PNAM"),  # xEdit wbDefinitionsFO4.pas:7223 wbArray
    ("NOCM", "NAM1"),  # xEdit wbDefinitionsFO4.pas:12466 wbString
    ("NOTE", "DNAM"),  # xEdit wbDefinitionsFO4.pas:12480 wbInteger
    ("NOTE", "PNAM"),  # xEdit wbDefinitionsFO4.pas:12498 wbString
    ("NPC_", "DNAM"),  # xEdit wbDefinitionsFO4.pas:10410 wbStruct
    ("NPC_", "NAM5"),  # xEdit wbDefinitionsFO4.pas:10422 wbUnknown
    ("NPC_", "NAM8"),  # xEdit wbDefinitionsFO4.pas:10431 wbInteger
    ("NPC_", "QNAM"),  # xEdit wbDefinitionsFO4.pas:10445 wbFloatRGBA
    ("OMOD", "NAM1"),  # xEdit wbDefinitionsFO4.pas:12542 wbInteger
    ("PACK", "FNAM"),  # xEdit wbDefinitionsFO4.pas:10616 wbInteger
    ("PACK", "XNAM"),  # xEdit wbDefinitionsFO4.pas:10601 wbByteArray
    ("PARW", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PBAR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PBEA", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PCON", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PERK", "FNAM"),  # xEdit wbDefinitionsFO4.pas:7605 wbString
    ("PFLA", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PGRE", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PHZD", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PKIN", "VNAM"),  # xEdit wbDefinitionsFO4.pas:12579 wbInteger
    ("PMIS", "MNAM"),  # xEdit wbDefinitionsFO4.pas:3264 wbString(ReferenceRecord)
    ("PROJ", "NAM1"),  # xEdit wbDefinitionsFO4.pas:7173 wbString
    ("PROJ", "VNAM"),  # xEdit wbDefinitionsFO4.pas:7176 wbInteger
    ("QUST", "ANAM"),  # xEdit wbDefinitionsFO4.pas:10805 wbInteger
    ("QUST", "CNAM"),  # xEdit wbDefinitionsFO4.pas:10758 wbLStringKC
    ("QUST", "DNAM"),  # xEdit wbDefinitionsFO4.pas:10691 wbStruct
    ("QUST", "FNAM"),  # xEdit wbDefinitionsFO4.pas:10774 wbInteger
    ("QUST", "NAM2"),  # xEdit wbDefinitionsFO4.pas:10757 wbString
    ("QUST", "NNAM"),  # xEdit wbDefinitionsFO4.pas:10778 wbLStringKC; wbDefinitionsFO4.pas:10943 wbString
    ("QUST", "SNAM"),  # xEdit wbDefinitionsFO4.pas:10945 wbString
    ("RACE", "ANAM"),  # xEdit wbDefinitionsFO4.pas:11196 wbString
    ("RACE", "HNAM"),  # xEdit wbDefinitionsFO4.pas:11340 wbString
    ("RACE", "NAME"),  # xEdit wbDefinitionsFO4.pas:11248 wbString
    ("RACE", "NNAM"),  # xEdit wbDefinitionsFO4.pas:11269 wbStruct; wbDefinitionsFO4.pas:11287 wbStruct
    ("RACE", "SRAF"),  # xEdit wbDefinitionsFO4.pas:11313 wbStruct
    ("RACE", "ZNAM"),  # xEdit wbDefinitionsFO4.pas:11033 wbString(wbEquipSlot)
    ("REFR", "MNAM"),  # xEdit wbDefinitionsFO4.pas:11865 wbString
    ("REVB", "ANAM"),  # xEdit wbDefinitionsFO4.pas:9609 wbInteger
    ("SCCO", "VNAM"),  # xEdit wbDefinitionsFO4.pas:12609 wbUnknown; wbDefinitionsFO4.pas:12618 wbUnknown
    ("SCCO", "XNAM"),  # xEdit wbDefinitionsFO4.pas:12599 wbStruct; wbDefinitionsFO4.pas:12611 wbStruct
    ("SCEN", "NNAM"),  # xEdit wbDefinitionsFO4.pas:9130 wbString
    ("SCEN", "VNAM"),  # xEdit wbDefinitionsFO4.pas:9107 wbStruct
    ("SCSN", "PNAM"),  # xEdit wbDefinitionsFO4.pas:12652 wbInteger
    ("SMBN", "DNAM"),  # xEdit wbDefinitionsFO4.pas:8726 wbInteger
    ("SMBN", "XNAM"),  # xEdit wbDefinitionsFO4.pas:8732 wbInteger
    ("SMEN", "DNAM"),  # xEdit wbDefinitionsFO4.pas:8768 wbInteger
    ("SMEN", "XNAM"),  # xEdit wbDefinitionsFO4.pas:8774 wbInteger
    ("SMQN", "MNAM"),  # xEdit wbDefinitionsFO4.pas:8751 wbInteger
    ("SMQN", "XNAM"),  # xEdit wbDefinitionsFO4.pas:8750 wbInteger
    ("SNCT", "VNAM"),  # xEdit wbDefinitionsFO4.pas:9483 wbInteger
    ("SNDR", "FNAM"),  # xEdit wbDefinitionsFO4.pas:9446 wbString
    ("SNDR", "NNAM"),  # xEdit wbDefinitionsFO4.pas:9408 wbString
    ("SOPM", "ONAM"),  # xEdit wbDefinitionsFO4.pas:9510 wbStruct
    ("SOPM", "VNAM"),  # xEdit wbDefinitionsFO4.pas:9509 wbInteger
    ("SPGD", "MNAM"),  # xEdit wbDefinitionsFO4.pas:6160 wbString
    ("STAT", "MNAM"),  # xEdit wbDefinitionsFO4.pas:12083 wbStruct
    ("TACT", "FNAM"),  # xEdit wbDefinitionsFO4.pas:5687 wbUnknown
    ("TERM", "ANAM"),  # xEdit wbDefinitionsFO4.pas:12731 wbInteger
    ("TERM", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12690 wbInteger
    ("TERM", "VNAM"),  # xEdit wbDefinitionsFO4.pas:12752 wbString
    ("TES4", "CNAM"),  # xEdit wbDefinitionsFO4.pas:12102 wbString
    ("TES4", "SNAM"),  # xEdit wbDefinitionsFO4.pas:12103 wbString
    ("TREE", "CNAM"),  # xEdit wbDefinitionsFO4.pas:12151 wbStruct
    ("TXST", "DNAM"),  # xEdit wbDefinitionsFO4.pas:6998 wbInteger
    ("TXST", "MNAM"),  # xEdit wbDefinitionsFO4.pas:7003 wbString
    ("VTYP", "DNAM"),  # xEdit wbDefinitionsFO4.pas:7952 wbInteger
    ("WATR", "ANAM"),  # xEdit wbDefinitionsFO4.pas:12791 wbInteger
    ("WATR", "DNAM"),  # xEdit wbDefinitionsFO4.pas:12803 wbStruct
    ("WATR", "FNAM"),  # xEdit wbDefinitionsFO4.pas:12792 wbInteger
    ("WATR", "GNAM"),  # xEdit wbDefinitionsFO4.pas:12869 wbByteArray
    ("WATR", "NAM2"),  # xEdit wbDefinitionsFO4.pas:12872 wbString
    ("WATR", "NAM3"),  # xEdit wbDefinitionsFO4.pas:12873 wbString
    ("WATR", "NAM4"),  # xEdit wbDefinitionsFO4.pas:12874 wbString
    ("WRLD", "DNAM"),  # xEdit wbDefinitionsCommon.pas:9726 wbStruct
    ("WRLD", "MNAM"),  # xEdit wbDefinitionsCommon.pas:9792 wbStruct
    ("WRLD", "NAM0"),  # xEdit wbDefinitionsCommon.pas:9910 wbStruct
    ("WRLD", "NNAM"),  # xEdit wbDefinitionsFO4.pas:13170 wbString
    ("WRLD", "ONAM"),  # xEdit wbDefinitionsCommon.pas:9836 wbStruct
    ("WRLD", "PNAM"),  # xEdit wbDefinitionsFO4.pas:13126 wbInteger
    ("WRLD", "TNAM"),  # xEdit wbDefinitionsFO4.pas:13172 wbString
    ("WTHR", "ANAM"),  # xEdit wbDefinitionsCommon.pas:9386 wbString
    ("WTHR", "BNAM"),  # xEdit wbDefinitionsCommon.pas:9388 wbString
    ("WTHR", "CNAM"),  # xEdit wbDefinitionsCommon.pas:9385 wbString
    ("WTHR", "DNAM"),  # xEdit wbDefinitionsCommon.pas:9384 wbString
    ("WTHR", "FNAM"),  # xEdit wbDefinitionsCommon.pas:9524 wbStruct
    ("WTHR", "JNAM"),  # xEdit wbDefinitionsCommon.pas:9306 wbArray
    ("WTHR", "LNAM"),  # xEdit wbDefinitionsFO4.pas:13040 wbInteger
    ("WTHR", "NAM0"),  # xEdit wbDefinitionsCommon.pas:9436 wbStruct
    ("WTHR", "NAM1"),  # xEdit wbDefinitionsCommon.pas:9511 wbInteger
    ("WTHR", "NAM4"),  # xEdit wbDefinitionsFO4.pas:13051 wbArray
    ("WTHR", "ONAM"),  # xEdit wbDefinitionsCommon.pas:9362 wbArray
    ("WTHR", "PNAM"),  # xEdit wbDefinitionsCommon.pas:9349 wbArray
    ("WTHR", "QNAM"),  # xEdit wbDefinitionsCommon.pas:9371 wbArray
    ("WTHR", "RNAM"),  # xEdit wbDefinitionsCommon.pas:9367 wbArray
    ("WTHR", "VNAM"),  # xEdit wbDefinitionsFO4.pas:13102 wbFloat
    ("WTHR", "WNAM"),  # xEdit wbDefinitionsFO4.pas:13105 wbFloat
]
FORM_ID_SUBRECORDS = pathlib.Path(__file__).resolve().parents[2] / "include/fo4/schema/formid_subrecords.inc"


def load_defs(paths):
    defs = "\n".join(open(path, encoding="utf-8", errors="replace").read() for path in paths)
    defs = re.sub(r"\{[^{}$]*\}", "", defs)
    return re.sub(r"//[^\n]*", "", defs)


def form_id_signatures(path):
    return re.findall(r"0x[0-9A-Fa-f]+u,\s*//\s*(\w{4})", path.read_text(encoding="utf-8"))


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
    blocks = collections.defaultdict(str)
    for m in re.finditer(r"wbRecord\((\w{4}),", defs):
        open_paren = m.start() + len("wbRecord")
        blocks[m.group(1)] += defs[m.start():open_paren] + bracket_span(defs, open_paren)
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


def static_scalar_pairs(defs, form_id_sigs):
    blocks = record_blocks(defs)
    return {(record, sub) for record, block in blocks.items() for sub in form_id_sigs
            if slot_is_scalar(defs, block, sub)}


def check_static(args):
    defs_path, form_id_path, *rest = args
    defs = load_defs([defs_path, *(p for p in rest if p.endswith(".pas"))])
    present = set()
    for path in (p for p in rest if p.endswith(".inc")):
        present |= set(re.findall(r'Signature::from_string\("(\w{4})"\),\s*Signature::from_string\("(\w{4})"\)',
                                  pathlib.Path(path).read_text(encoding="utf-8")))
    missing = sorted(static_scalar_pairs(defs, form_id_signatures(pathlib.Path(form_id_path))) - present)
    for record, subrecord in missing:
        print(f"missing {record} {subrecord}")
    return 1 if missing else 0


def main():
    if sys.argv[1] == "--check-static":
        sys.exit(check_static(sys.argv[2:]))
    tool, data_dir, defs_path, output = sys.argv[1], pathlib.Path(sys.argv[2]), sys.argv[3], pathlib.Path(sys.argv[4])
    defs = load_defs([defs_path, *sys.argv[5:]])
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
    static = static_scalar_pairs(defs, form_id_signatures(FORM_ID_SUBRECORDS))
    denied.update({pair: "xEdit" for pair in static if pair not in denied})
    denied.update({pair: "xEdit" for pair in XEDIT_DENIED})
    denied.update({pair: "xedit" for pair in XEDIT_ONLY_SCALAR_SLOTS if pair not in denied})

    lines = [
        "// Generated by Radicle/tools/corpus/non_formid_slots.py: slots where the official masters store",
        "// values that resolve to no official record AND xEdit wbDefinitionsFO4.pas declares the subrecord",
        "// in that record only as non-form-ID data. Entries marked xEdit come from XEDIT_DENIED or the static pass, not the corpus.",
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
