#!/usr/bin/env python3

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCOPES = ROOT / "Radicle" / "generated" / "fo4" / "schema" / "generated_condition_scopes.inc"
CONDITION_SCOPES = ROOT / "Radicle" / "src" / "schema" / "condition_scopes.cpp"
STATIC_COLLECTION = ROOT / "Radicle" / "src" / "forms" / "static_collection.cpp"
COLLECTION_RUN_FILES = [
    ROOT / "Radicle" / "src" / "forms" / "quest_aliases_decode.cpp",
    ROOT / "Radicle" / "src" / "forms" / "quest_alias_edit.cpp",
    CONDITION_SCOPES,
]
COMPONENTS = ROOT / "PluginEditTool" / "native" / "src" / "plugin" / "RadiclePluginComponents.cpp"

ALIAS_EXPANSION = {"wbKeywords": ["KSIZ", "KWDA"], "wbCOCT": ["COCT"], "wbCNTOs": ["CNTO", "COED"]}
ALIAS_MEMBER = re.compile(r"\b(wbKeywords|wbCOCT|wbCNTOs)\b|\bwb\w+\(([A-Z][A-Z0-9_]{3}),")
QUOTED = re.compile(r'"([A-Z][A-Z0-9_]{3})"')


def quoted_list(text: str, start_pattern: str) -> list:
    match = re.search(start_pattern + r"[^{]*\{(.*?)\};", text, re.S)
    if match is None:
        raise SystemExit(f"pattern not found: {start_pattern}")
    return QUOTED.findall(match.group(1))


def check_effects(xedit: str, problems: list) -> None:
    if not re.search(r"wbRStruct\('Effect', \[\s*wbEFID,\s*wbEFIT,\s*wbConditions\s*\]", xedit):
        problems.append("effect struct is no longer wbEFID, wbEFIT, wbConditions")
    if not re.search(r"wbEFID := wbFormIDCk\(EFID, 'Base Effect', \[MGEF\]\);", xedit):
        problems.append("wbEFID is no longer an optional member")
    efit = re.search(r"wbEFIT :=([^;]*);", xedit)
    if efit is None or ".SetRequired" not in efit.group(1):
        problems.append("wbEFIT is no longer required")
    users = set()
    for hit in re.finditer(r"^\s*wbEffectsReq\s*,?\s*$", xedit, re.M):
        users.add(re.findall(r"wbRecord\((\w{4}),", xedit[: hit.start()])[-1])
    ours = set(re.findall(r'from_string\("(\w{4})"\), "Effects", "Conditions", ConditionScopeKind::effect', SCOPES.read_text(encoding="utf-8")))
    if users != ours:
        problems.append(f"effect records: xEdit {sorted(users)} Radicle {sorted(ours)}")


def check_alias_order(xedit: str, problems: list) -> None:
    start = xedit.index("'Reference Alias', [")
    body = xedit[xedit.index("wbKeywords,", start): xedit.index("wbEmpty(ALED", start)]
    expected = []
    for match in ALIAS_MEMBER.finditer(body):
        expected += ALIAS_EXPANSION.get(match.group(1)) or [match.group(2)]
    ours = quoted_list(CONDITION_SCOPES.read_text(encoding="utf-8"), r"k_reference_condition_successors\[\]")
    if expected != ours:
        problems.append(f"reference alias member order: xEdit {expected} Radicle {ours}")
    if not re.search(r"'Ref Collection Alias', \[\s*wbInteger\(ALCS, 'Alias ID', itU32\),\s*wbInteger\(ALMI, 'Max Initial Fill Count', itU8\)\s*\]\)", xedit):
        problems.append("collection alias is no longer the struct [ALCS, ALMI]")
    for path in COLLECTION_RUN_FILES:
        if re.search(r"while \([^{;]*sig_alcs\)", path.read_text(encoding="utf-8")):
            problems.append(f"{path.name} groups a run of ALCS into one alias; xEdit makes one alias per ALCS")


def check_scol(xedit: str, problems: list) -> None:
    match = re.search(r"wbStaticPart :=\s*wbRStructSK\(\[0\], 'Part', \[\s*wbFormIDCk\(ONAM, 'Static', \[(.*?)\]\)", xedit, re.S)
    if match is None:
        problems.append("wbStaticPart ONAM list not found")
        return
    theirs = set(re.findall(r"\b([A-Z][A-Z0-9_]{3})\b", match.group(1)))
    ours = set(quoted_list(STATIC_COLLECTION.read_text(encoding="utf-8"), r"k_static_part_targets\[\]"))
    if theirs != ours:
        problems.append(f"SCOL part targets: xEdit {sorted(theirs)} Radicle {sorted(ours)}")


def check_components(xedit: str, problems: list) -> None:
    base = re.search(r"sigBaseObjects\s*:\s*TwbSignatures\s*=\s*\[(.*?)\];", xedit, re.S)
    allowed = set(re.findall(r"'(\w{4})'", base.group(1))) if base else set()
    if len(re.findall(r"wbFormIDCkNoReach\('Component', sigBaseObjects\)", xedit)) != 2:
        problems.append("COBJ, MISC component members no longer all use sigBaseObjects")
    ours = set(quoted_list(COMPONENTS.read_text(encoding="utf-8"), r"kItems\[\]"))
    outside = sorted(ours - allowed)
    if outside:
        problems.append(f"SetComponents item signatures outside sigBaseObjects: {outside}")


def check_quest_vmad(xedit: str, problems: list) -> None:
    start = xedit.index("wbVMADFragmentedQUST := wbStruct(VMAD")
    block = xedit[start: xedit.index("wbVMADFragmentedSCEN", start)]
    fragment = re.search(r"wbArrayS\('Fragments',\s*wbStructSK\(\[0, 1\], 'Fragment', \[(.*?)\]\)", block, re.S)
    members = re.findall(r"wb(?:Integer|Unknown|LenString)\(([^)]*)\)", fragment.group(1)) if fragment else []
    if members != ["'Quest Stage', itU32", "'Quest Stage Index', itU32", "1", "'ScriptName', 2", "'FragmentName', 2"]:
        problems.append(f"QUST fragment members changed: {members}")
    if not re.search(r"wbInteger\('FragmentCount', itU16", block) or "wbArrayS('Aliases'" not in block:
        problems.append("QUST VMAD fragment count or alias array changed")


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare Radicle format rules with the pinned xEdit definitions.")
    parser.add_argument("xedit_definitions", type=Path, help="Core/wbDefinitionsFO4.pas from the pinned xEdit checkout")
    args = parser.parse_args()
    xedit = args.xedit_definitions.read_text(encoding="latin-1")

    problems = []
    for check in (check_effects, check_alias_order, check_scol, check_components, check_quest_vmad):
        check(xedit, problems)

    for line in problems:
        print(line)
    print(f"5 rule groups checked, {len(problems)} differences")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
