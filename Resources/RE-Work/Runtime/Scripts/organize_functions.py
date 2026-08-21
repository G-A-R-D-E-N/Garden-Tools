#!/usr/bin/env python3
"""
Subsystem-bucketing + cross-reference pass over the workspace's raw RE symbol dumps.

Inputs:
  - ReverseEngineering/Symbols/Fallout4_functions.txt        1.10.155 named project dump, 411,713 funcs, real MS-demangled sigs
  - ReverseEngineering/Symbols/Fallout4_data.txt              1.10.155 named project dump, 82,391 data symbols
  - ReverseEngineering/Symbols/1.10.163/Fallout4_163_functions.txt   1.10.163 (our actual target) Ghidra DB export,
                                              365,059 funcs, "Class::Method :: <prototype>" format
  - ReverseEngineering/Projects/fwgh_proj_1.10.163_unpacked/label_apply_disagreements.csv
                                              40,073 rows where the community IDC label disagreed
                                              with the existing (CommonLib-RTTI-derived) name and
                                              was NOT auto-applied -- see ReverseEngineering/
                                              05-LABEL-RECOVERY.md section 3.

Outputs (all under ReverseEngineering/Organized/):
  - 1.10.155/<Category>.txt, 1.10.163/<Category>.txt   bucketed function lists, one category per file
  - INDEX.md                                            counts + description per category, both versions
  - crossref_163_disagreements_vs_155.csv               each disagreement row + a verdict on whether
                                                          the 1.10.155 named project's real names corroborate the
                                                          existing (CommonLib) name, the community-decoded
                                                          name, both, or neither
  - crossref_review_candidates.md                       the disagreement rows flagged HIGH PRIORITY:
                                                          note=="function-name-conflict" AND the
                                                          community-decoded name is corroborated by 155
                                                          but the CURRENT (CommonLib) name is NOT --
                                                          i.e. candidates for the same class of real bug
                                                          already caught twice (PlayerCharacter::
                                                          ServePrisonTime / GoToPrison were both wrong).

Categorization is prefix/keyword-based on each function's top-level class (the token before the
first "::"), not a full namespace model -- FO4's engine doesn't have subsystem namespaces, just
class-name conventions (hk*/bhk* = Havok, TES*= Elder Scrolls forms, BGS* = Bethesda Game Studio
records, Ni* = NetImmerse/Gamebryo graphics, BST* = Bethesda template containers, ...). Good enough
to make 400k+ flat function names actually browsable by topic; not a precise ontology.
"""
import re
import os
import csv
from collections import defaultdict, Counter

ROOT = r"E:\F4SE OG"
SYM155_FUNCS = os.path.join(ROOT, "ReverseEngineering", "Symbols", "Fallout4_functions.txt")
SYM155_DATA  = os.path.join(ROOT, "ReverseEngineering", "Symbols", "Fallout4_data.txt")
SYM163_FUNCS = os.path.join(ROOT, "ReverseEngineering", "Symbols", "1.10.163", "Fallout4_163_functions.txt")
DISAGREEMENTS = os.path.join(ROOT, "ReverseEngineering", "Projects", "fwgh_proj_1.10.163_unpacked", "label_apply_disagreements.csv")
OUT_DIR = os.path.join(ROOT, "ReverseEngineering", "Organized")
OUT_155 = os.path.join(OUT_DIR, "1.10.155")
OUT_163 = os.path.join(OUT_DIR, "1.10.163")

os.makedirs(OUT_155, exist_ok=True)
os.makedirs(OUT_163, exist_ok=True)

# ── Category rules (ordered, first match wins) ──────────────────────────────
# Each rule: (category_key, description, predicate(top_class, full_name) -> bool)
def starts(prefix):
    return lambda tc, fn: tc.startswith(prefix)

def contains_any(*subs):
    return lambda tc, fn: any(s in fn for s in subs)

CATEGORIES = [
    ("Havok_Animation",   "Havok Behavior (hkb*) -- animation state machines",
        starts("hkb")),
    ("Havok_Physics",     "Havok/NVFlex physics (hk*, bhk*, hkp*, hkn*)",
        lambda tc, fn: tc.startswith("hk") or tc.startswith("bhk") or tc.startswith("NVFlex")),
    ("UI_Scaleform",      "Scaleform/GFx/AS3 UI plumbing",
        lambda tc, fn: tc.startswith("Scaleform") or tc.startswith("GFx") or tc.startswith("fl::") or "Scaleform::" in fn or "GFx::" in fn),
    ("UI_Menu_HUD",       "Menu/HUD classes (not Scaleform internals)",
        contains_any("Menu", "HUD", "Pipboy", "Compass")),
    ("Scripting_Papyrus",  "Papyrus VM / BSScript",
        lambda tc, fn: tc.startswith("BSScript") or tc.startswith("Papyrus") or "VirtualMachine" in fn),
    ("Animation",          "Animation graphs / idles / controller sequences (non-Havok)",
        contains_any("AnimationGraph", "AnimGraph", "IdleForm", "NiControllerSequence", "AnimationFileManager", "BSAnimation")),
    ("Weather",             "Sky/TESWeather/TESClimate/Clouds/Precipitation/Atmosphere/Stars/Sun/Moon/SkySound",
        lambda tc, fn: tc.startswith("Sky") or tc.startswith("Clouds") or tc.startswith("Precipitation")
                       or tc.startswith("Atmosphere") or tc.startswith("Stars") or tc.startswith("TESWeather")
                       or tc.startswith("TESClimate") or tc.startswith("WeatherEntry") or tc.startswith("Sun")
                       or tc.startswith("Moon") or contains_any("TESRegionDataWeather", "WEATHER_")(tc, fn)),
    ("Audio",               "Sound/music/voice",
        contains_any("Sound", "Audio", "Music", "Voice", "Wwise")),
    ("AI_Combat",           "AI process lists, combat, detection, packages, aim model, AI timers/formulas",
        lambda tc, fn: tc.startswith("AI") or contains_any(
            "Combat", "Detection", "AIProcess", "Package", "ProcessLists", "HighProcess",
            "MidHighProcess", "MiddleProcess", "MidLowProcess", "LowProcess", "AIWorldLocation",
            "DetectionManager", "CombatController", "AimModel", "TESAIForm")(tc, fn)),
    ("Quests",              "Quest/alias/dialogue-topic/scene/story-manager system",
        contains_any("Quest", "Alias", "StoryManager", "Topic", "BGSScene", "SceneAction",
                      "ScenePhase", "SCENE_", "QUEST_")),
    ("Pathing_Navmesh",     "Pathfinding / navmesh",
        contains_any("Path", "NavMesh", "Pathing")),
    ("SaveLoad",            "Save/load pipeline",
        contains_any("SaveLoad", "SaveGame", "CoSave", "LoadFormBuffer", "SaveFormBuffer", "BGSSaveLoadManager")),
    ("Input",               "Input events / player controls",
        contains_any("InputEvent", "PlayerControls", "ButtonEvent", "InputManager")),
    ("Forms_TES",           "TES*-prefixed form/record classes",
        starts("TES")),
    ("Forms_BGS",           "BGS*-prefixed Bethesda Game Studio record/system classes",
        starts("BGS")),
    ("Actor_Character",     "Actor/Character/NPC/PlayerCharacter",
        contains_any("Actor", "Character", "PlayerCharacter", "NPC")),
    ("Graphics_NetImmerse", "Ni*-prefixed NetImmerse/Gamebryo scenegraph & graphics",
        starts("Ni")),
    ("Graphics_BS",         "BSGraphics/BSShader/BSGeometry/BSRender",
        lambda tc, fn: tc.startswith("BSGraphics") or tc.startswith("BSShader") or tc.startswith("BSGeometry") or tc.startswith("BSRender")),
    ("STL",                 "std:: standard library instantiations",
        starts("std")),
    ("BS_Templates",        "BST*-prefixed template containers (BSTArray, BSTScatterTable, ...)",
        starts("BST")),
    ("BS_Common",           "Other BS*-prefixed Bethesda common-library classes",
        starts("BS")),
]
MISC_KEY = "Misc_Uncategorized"

def categorize(top_class, full_name):
    for key, _desc, pred in CATEGORIES:
        try:
            if pred(top_class, full_name):
                return key
        except Exception:
            continue
    return MISC_KEY

# ── Name extraction ──────────────────────────────────────────────────────────
CC_RE = re.compile(r"__(?:cdecl|stdcall|thiscall|fastcall|vectorcall)\s+(.*)")

def extract_name_from_155_line(sig_tail):
    """1.10.155 lines carry a full demangled C++ signature after the calling convention
    keyword. Walk forward tracking <...> depth so template args don't confuse the first '('
    that actually starts the argument list."""
    m = CC_RE.search(sig_tail)
    text = m.group(1) if m else sig_tail
    depth = 0
    for i, ch in enumerate(text):
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth = max(0, depth - 1)
        elif ch == '(' and depth == 0:
            return text[:i].strip()
    return text.strip()

QUALIFIED_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*(?:::[~]?[A-Za-z_][A-Za-z0-9_]*)+")

def qualified_tokens(text):
    return set(QUALIFIED_TOKEN_RE.findall(text))

def top_class_of(qualified_name):
    return qualified_name.split("::", 1)[0]

# ── Pass 1: parse 1.10.155 functions.txt, bucket + collect name set ─────────
FUNC155_RE = re.compile(r"^\s*func \[0x[0-9a-fA-F]+[+\-][^\]]*\|\s*sizeof=\s*(\d+)\]\s*(.*)$")

print("Parsing 1.10.155 functions...")
names155_func = set()
buckets155_func = defaultdict(list)
cat_counts_155f = Counter()
total155f = 0
with open(SYM155_FUNCS, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = FUNC155_RE.match(line)
        if not m:
            continue
        total155f += 1
        sig_tail = m.group(2)
        qname = extract_name_from_155_line(sig_tail)
        if "::" in qname:
            names155_func.add(qname)
            tc = top_class_of(qname)
        else:
            tc = qname
        cat = categorize(tc, qname)
        cat_counts_155f[cat] += 1
        buckets155_func[cat].append(line.rstrip("\n"))
print(f"  {total155f} functions parsed, {len(names155_func)} qualified Class::Method names")

print("Parsing 1.10.155 data symbols (for cross-ref only, not bucketed)...")
DATA155_RE = re.compile(r"^\s*data \[[^\]]*\]\s*(?:\[static\]\s*)?(.*)$")
names155_data = set()
total155d = 0
with open(SYM155_DATA, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = DATA155_RE.match(line)
        if not m:
            continue
        total155d += 1
        for tok in qualified_tokens(m.group(1)):
            names155_data.add(tok)
print(f"  {total155d} data symbols parsed, {len(names155_data)} qualified tokens")

# ── Pass 2: parse 1.10.163 export, bucket + collect name set ────────────────
# Split on the literal " :: " separator FW_ExportAll.java writes between the qualified name and
# the prototype (a regex first-"::"  split point would be wrong -- the qualified name itself
# contains "::" internally).

print("Parsing 1.10.163 functions...")
names163_func = set()
buckets163_func = defaultdict(list)
cat_counts_163f = Counter()
total163f = 0
with open(SYM163_FUNCS, "r", encoding="utf-8", errors="replace") as f:
    for line in f:
        if not line.lstrip().startswith("func ["):
            continue
        if " :: " not in line:
            continue
        head, _proto = line.rstrip("\n").split(" :: ", 1)
        # head = "  func [0xRVA | sizeof=N] Class::Method"
        rb = head.rfind("]")
        if rb == -1:
            continue
        qname = head[rb + 1:].strip()
        total163f += 1
        if "::" in qname:
            names163_func.add(qname)
            tc = top_class_of(qname)
        else:
            tc = qname
        cat = categorize(tc, qname)
        cat_counts_163f[cat] += 1
        buckets163_func[cat].append(line.rstrip("\n"))
print(f"  {total163f} functions parsed, {len(names163_func)} qualified Class::Method names")

# ── Write bucketed files ─────────────────────────────────────────────────────
def write_buckets(buckets, out_dir, version_label, source_file):
    for cat, lines in buckets.items():
        path = os.path.join(out_dir, f"{cat}.txt")
        with open(path, "w", encoding="utf-8") as w:
            w.write(f"# {version_label} -- category: {cat} -- {len(lines)} functions\n")
            w.write(f"# source: {source_file}\n")
            w.write("# regenerate with ReverseEngineering/Scripts/organize_functions.py\n\n")
            for l in lines:
                w.write(l + "\n")

print("Writing 1.10.155 bucket files...")
write_buckets(buckets155_func, OUT_155, "1.10.155", SYM155_FUNCS)
print("Writing 1.10.163 bucket files...")
write_buckets(buckets163_func, OUT_163, "1.10.163", SYM163_FUNCS)

# ── INDEX.md ──────────────────────────────────────────────────────────────
index_path = os.path.join(OUT_DIR, "INDEX.md")
all_cats = [c[0] for c in CATEGORIES] + [MISC_KEY]
desc_map = {c[0]: c[1] for c in CATEGORIES}
desc_map[MISC_KEY] = "Everything not matched by a more specific rule above"

with open(index_path, "w", encoding="utf-8") as w:
    w.write("# Function subsystem index\n\n")
    w.write("Auto-generated by `ReverseEngineering/Scripts/organize_functions.py`. Buckets 1.10.155 named project names "
            "(411,713 functions, real MS-demangled signatures -- WRONG addresses for our actual "
            "target) and 1.10.163 Ghidra-export names (365,059 functions, real addresses -- **this "
            "is the build we actually hook**) by top-level class-name convention, so the flat dumps "
            "are browsable by topic instead of one giant alphabetical list.\n\n")
    w.write("Regenerate after either source file changes: `python ReverseEngineering/Scripts/organize_functions.py`\n\n")
    w.write("| Category | Description | 1.10.155 count | 1.10.163 count | Files |\n")
    w.write("|---|---|---:|---:|---|\n")
    for cat in all_cats:
        c155 = cat_counts_155f.get(cat, 0)
        c163 = cat_counts_163f.get(cat, 0)
        w.write(f"| `{cat}` | {desc_map[cat]} | {c155:,} | {c163:,} | "
                f"[1.10.155](1.10.155/{cat}.txt) / [1.10.163](1.10.163/{cat}.txt) |\n")
    w.write(f"\n**Totals:** 1.10.155 = {total155f:,} functions parsed; "
            f"1.10.163 = {total163f:,} functions parsed.\n")
    w.write("\n## Deep-dive docs\n\n"
            "Narrative architecture write-ups (decompile-verified, not just bucketed function "
            "names) for select subsystems, in `1.10.163/`:\n\n"
            "- [`README-Quests.md`](1.10.163/README-Quests.md) -- TESQuest/alias/story-manager/scene system\n"
            "- [`README-AI.md`](1.10.163/README-AI.md) -- AIProcess/package-eval/combat-behavior-tree/detection\n"
            "- [`README-Pipboy.md`](1.10.163/README-Pipboy.md) -- MiscStatManager array layout, PipboyRadioController::SetRadioStation\n"
            "- [`README-Controllers.md`](1.10.163/README-Controllers.md) -- button-prompt glyph table (Xenon_*/PSN_*), the full UserEvent-to-glyph resolution chain\n"
            "- [`README-Weather.md`](1.10.163/README-Weather.md) -- Sky/TESWeather/TESClimate: transition-percent formula, fogData offset map, ThunderFreq CTD root cause, weighted weather roll\n")

print(f"Wrote {index_path}")

# ── Cross-reference: label_apply_disagreements.csv vs 1.10.155 real names ──
print("Cross-referencing 1.10.163 disagreements against 1.10.155 named project names...")
names155_all = names155_func | names155_data

# CommonLib's RTTI-vtable-slot matcher falls back to a generic placeholder name
# (Class::FuncN, Class::FuncN_v2, a bare "_NNN", "thunk_FUN_...", or an empty "Class::" with no
# method) whenever it can't resolve a slot -- that is an HONEST "unknown", not a wrong guess, and
# 05-LABEL-RECOVERY.md already calls filling these in a "safe upgrade a human could apply". Only
# a SPECIFIC, confident-looking existing name that still conflicts with a 155-confirmed decoded
# name matches the dangerous class the two known real bugs belong to (CommonLib was confident
# AND wrong). Split on that distinction so the small, high-value list isn't buried in the much
# larger, low-risk "naming gap" list.
PLACEHOLDER_RE = re.compile(
    r'^(_\d+|FUN_[0-9a-fA-F]+|sub_[0-9a-fA-F]+|thunk_FUN_[0-9a-fA-F]+|'
    r'.*::Func\d+(_v\d+)?|.*::_\d+|.*::)$'
)

def is_placeholder_name(name):
    return bool(PLACEHOLDER_RE.match(name.strip()))

def bare_tail(name):
    """Trailing identifier after the last '::', ignoring trailing junk. Used to catch the case
    where CommonLib's existing name is a legitimate but UNQUALIFIED method name (e.g. "Call",
    "PostLinkObject", "LinkObject") that the qualified-token matcher can't verify against 155
    (no "::" to extract a token from) -- without this, those show up as false "misname"
    positives just because they're missing their class prefix, not because they're wrong."""
    s = name.strip().rstrip(':').strip()
    if "::" in s:
        s = s.rsplit("::", 1)[-1]
    return s

crossref_path = os.path.join(OUT_DIR, "crossref_163_disagreements_vs_155.csv")
review_path = os.path.join(OUT_DIR, "crossref_review_candidates.md")
gaps_path = os.path.join(OUT_DIR, "crossref_safe_naming_gaps.md")

rows_out = []
genuine_misname_candidates = []   # existing name is specific/confident AND wrong -- dangerous class
safe_naming_gaps = []             # existing name is a generic placeholder -- safe to just apply
unqualified_matches = 0           # existing name lacks "::" but its bare method name still matches

with open(DISAGREEMENTS, "r", encoding="utf-8", errors="replace", newline="") as f:
    reader = csv.DictReader(f)
    for row in reader:
        addr = row.get("address", "")
        existing = row.get("existing_name", "")
        decoded = row.get("decoded_name", "")
        note = row.get("note", "")

        existing_tokens = qualified_tokens(existing) or {existing}
        decoded_tokens = qualified_tokens(decoded)

        match_existing = any(t in names155_all for t in existing_tokens)
        match_decoded_tokens = [t for t in decoded_tokens if t in names155_all]
        match_decoded = bool(match_decoded_tokens)

        # Bare-method-name fallback: if existing has no "::" (so the token matcher had nothing
        # to check) and its bare identifier matches the bare tail of a 155-confirmed decoded
        # token, treat existing as effectively confirmed too -- it's just missing qualification,
        # not wrong.
        if not match_existing and "::" not in existing and match_decoded_tokens:
            existing_bare = bare_tail(existing)
            if existing_bare and any(bare_tail(t) == existing_bare for t in match_decoded_tokens):
                match_existing = True
                unqualified_matches += 1

        if match_decoded and not match_existing:
            verdict = "decoded_confirmed_by_155"
        elif match_existing and not match_decoded:
            verdict = "existing_confirmed_by_155"
        elif match_existing and match_decoded:
            verdict = "both_confirmed"
        else:
            verdict = "neither_confirmed"

        rows_out.append([addr, existing, decoded, note, verdict])

        if verdict == "decoded_confirmed_by_155" and note == "function-name-conflict":
            if is_placeholder_name(existing):
                safe_naming_gaps.append((addr, existing, decoded))
            else:
                genuine_misname_candidates.append((addr, existing, decoded))

with open(crossref_path, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w)
    writer.writerow(["address", "existing_name", "decoded_name", "note", "verdict_vs_1.10.155"])
    writer.writerows(rows_out)

verdict_counts = Counter(r[4] for r in rows_out)
print(f"  {len(rows_out)} disagreement rows cross-referenced")
for v, c in verdict_counts.most_common():
    print(f"    {v}: {c}")
print(f"  (of which, existing name was unqualified but its bare method name matched: {unqualified_matches})")
print(f"  GENUINE MISNAME candidates (specific existing name, still contradicted by 155): "
      f"{len(genuine_misname_candidates)}")
print(f"  Safe naming-gap candidates (existing name is a generic placeholder): "
      f"{len(safe_naming_gaps)}")

with open(review_path, "w", encoding="utf-8") as w:
    w.write("# High-priority review candidates: likely-wrong CommonLib RTTI names\n\n")
    w.write("Generated by `ReverseEngineering/Scripts/organize_functions.py`. These are "
            "`label_apply_disagreements.csv` rows (community IDC label disagreed with the existing "
            "name and was NOT auto-applied -- see `ReverseEngineering/05-LABEL-RECOVERY.md` "
            "section 3) where:\n\n"
            "1. `note == \"function-name-conflict\"` (not a cosmetic vtable/RTTI-label formatting diff), AND\n"
            "2. the community-decoded name has an exact qualified-name match in the independent "
            "1.10.155 named project (real Microsoft-verified symbols), AND\n"
            "3. the CURRENT (CommonLib-RTTI-derived) name has NO match in 1.10.155, AND\n"
            "4. the current name is NOT a generic CommonLib placeholder (`Class::FuncN`, `_NNN`, "
            "`thunk_FUN_...`, empty `Class::`) -- those are honest \"unknown\" markers, not wrong "
            "guesses; see `crossref_safe_naming_gaps.md` for that much larger, low-risk list instead.\n\n"
            "This filtered list is exactly the signature of the two already-confirmed real bugs "
            "(`PlayerCharacter::ServePrisonTime` is actually `PlayerCharacter::Revert`; "
            "`PlayerCharacter::GoToPrison` is actually `PlayerCharacter::FinishLoadGame`) -- "
            "CommonLib's RTTI-vtable matcher was CONFIDENT and WRONG. The 1.10.155 named project is a THIRD, "
            "independent source, so 155-confirms-decoded-but-not-existing is meaningfully stronger "
            "evidence than the disagreement alone. **Not yet individually verified by decompile** -- "
            "this is a prioritized candidate list, not a confirmed-bug list. Before trusting/renaming, "
            "decompile the address and confirm sane behavior (`run_query.ps1 "
            "\"decompile:<existing_name>\"` or `addr:0x<RVA>,decompile`).\n\n")
    w.write(f"**{len(genuine_misname_candidates)} candidates found.**\n\n")
    w.write("| Address (163) | Current (CommonLib) name | 1.10.155-confirmed decoded name |\n")
    w.write("|---|---|---|\n")
    for addr, existing, decoded in genuine_misname_candidates:
        dshort = decoded if len(decoded) < 120 else decoded[:117] + "..."
        w.write(f"| `{addr}` | `{existing}` | `{dshort}` |\n")

with open(gaps_path, "w", encoding="utf-8") as w:
    w.write("# Safe naming-gap candidates: real names for generic CommonLib placeholders\n\n")
    w.write("Generated by `ReverseEngineering/Scripts/organize_functions.py`. Same filter as "
            "`crossref_review_candidates.md` except the CURRENT (CommonLib) name IS a generic "
            "placeholder (`Class::FuncN`, bare `_NNN`, `thunk_FUN_...`, empty `Class::`) -- "
            "05-LABEL-RECOVERY.md already characterizes this class as \"CommonLib doesn't know the "
            "real method for many vtable slots... these are safe upgrades a human could apply\". "
            "Low risk (no existing confident claim is being overturned, just filled in), high volume. "
            "Good candidates for a batch CommonLib header update, not individual decompile "
            "verification.\n\n")
    w.write(f"**{len(safe_naming_gaps)} candidates found.**\n\n")
    w.write("| Address (163) | Current (placeholder) name | 1.10.155-confirmed decoded name |\n")
    w.write("|---|---|---|\n")
    for addr, existing, decoded in safe_naming_gaps:
        dshort = decoded if len(decoded) < 120 else decoded[:117] + "..."
        w.write(f"| `{addr}` | `{existing}` | `{dshort}` |\n")

candidates_csv_path = os.path.join(OUT_DIR, "crossref_review_candidates.csv")
with open(candidates_csv_path, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w)
    writer.writerow(["address", "existing_name", "decoded_name"])
    writer.writerows(genuine_misname_candidates)
print(f"Wrote {candidates_csv_path}  (machine-readable version of crossref_review_candidates.md, for the decompile-batch script)")

gaps_csv_path = os.path.join(OUT_DIR, "crossref_safe_naming_gaps.csv")
with open(gaps_csv_path, "w", encoding="utf-8", newline="") as w:
    writer = csv.writer(w)
    writer.writerow(["address", "existing_name", "decoded_name"])
    writer.writerows(safe_naming_gaps)
print(f"Wrote {gaps_csv_path}  (machine-readable version of crossref_safe_naming_gaps.md, for the batch-apply script)")

print(f"Wrote {crossref_path}")
print(f"Wrote {review_path}")
print(f"Wrote {gaps_path}")
print("Done.")
