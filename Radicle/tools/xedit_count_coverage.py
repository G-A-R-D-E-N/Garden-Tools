"""Coverage report for the xEdit counted-array inventory (#602 D-26).

Every CountPath/SetCountPath site in the pinned wbDefinitionsFO4.pas must be
owned by the schema through one of its existing conventions:

* a counted_array or subrecord_group row whose count_subrecord is the site;
* a subrecord_group run whose count_member is the site;
* a coupled authoring rule, which keeps the counter out of generic add, remove
  and scalar writes and leaves it to the typed list editor;
* a formlink list counter, which the typed link editor rewrites with the list.

Named struct paths are listed in MODELLED_PATHS/MODELLED_LINES with the row or
mechanism that carries them. The fragment count is owned by the fragment
writers, which keep the indexed count word and the flag-keyed bits in step
(QUST, PERK and TERM carry a count word; PACK, INFO and SCEN key their
fragments off the flags byte). The OMOD property count is owned by the OMOD
Properties run row that sizes its DATA rows, and the OBTS include and property
counts by the writer that rebuilds that template, so each flips with its owner.
--strict fails on an unclassified site, so a new count site cannot enter the
schema unnoticed.

This is a coverage guard, not a build gate.
"""

import argparse
import json
import sys
from pathlib import Path

# Constraint: Count sites whose element layout no schema row can carry yet. Keyed by the count subrecord or named path the definition declares.
VARIABLE_LENGTH_ELEMENTS = {
    "MPPC": "morph presets carry lstrings and are nested per MPGN morph group",
    "ITMC": "rates-of-fire carry an FNAM string between ITMS/ITME markers",
    "LFSP": "lens flare entries carry DNAM/FNAM strings, so elements are not fixed-stride",
    "BSIZ": "body text entries carry BTXT lstrings, so elements are not fixed-stride",
    "ISIZ": "menu items carry ITXT/RNAM/UNAM/VNAM strings, so elements are not fixed-stride",
    "VNAM": "sound mapping entries carry variable-length subrecords",
}

U8_COUNTERS = {
    "LLCT": "xEdit declares the count as a 1-byte integer; the counted_array model requires a 4-byte counter",
}

# Constraint: Named paths that a Radicle row or typed owner already counts. XCNT words are owned by the FSTS footstep formlink lists, so classify() also checks owners.
MODELLED_PATHS = {
    "Meshes Count": "CELL\\CombinedMeshIds and CELL\\CombinedReferences count the XCRI header words",
    "DATA\\Counter Effect Count": "MGEF\\CounterEffects formlink list carries the DATA count",
}

# Constraint: the OBTS include and property counts have no writable schema row, so their owner is the row editor that rewrites the count words when it inserts, clones or removes an include, keyword or property.
OBTS_COUNT_PATHS = ("Include Count",)
OBTS_COUNT_FILES = ("src/forms/object_template.cpp",)
OBTS_COUNT_TOKENS = ("insert_object_template_row", "remove_object_template_row", "next[15]", "next[count_offset + i]")
OBTS_COUNT_REASON = (
    "the OBTS include and property count words are rewritten by insert_object_template_row and "
    "remove_object_template_row in src/forms/object_template.cpp"
)


# Constraint: a VMAD fragment count is mapped only while the writer that owns each shape is in the tree, so ownership is read from the code.
VMAD_INDEXED_COUNT_FILES = ("src/plugin/vmad_fragment_edits.cpp",
                            "src/plugin/vmad_writer.cpp", "src/schema/vmad.cpp")
VMAD_INDEXED_COUNT_TOKENS = ("count_offset", "section.indexed", "extras.count_offset",
                             "walk_indexed_fragments")
VMAD_INDEXED_COUNT_REASON = (
    "the indexed count word is rewritten by src/plugin/vmad_fragment_edits.cpp (PERK/TERM) "
    "and src/plugin/vmad_writer.cpp (QUST)"
)
VMAD_FLAGGED_COUNT_FILES = ("src/plugin/vmad_fragment_edits.cpp", "src/schema/vmad.cpp")
VMAD_FLAGGED_COUNT_TOKENS = ("fragment_flag_bits", "flags_offset", "walk_flagged_fragments")
VMAD_FLAGGED_COUNT_REASON = (
    "the flag-keyed fragments are set and cleared by src/plugin/vmad_fragment_edits.cpp "
    "and decoded by walk_flagged_fragments"
)
# Constraint: the OMOD property count is a DATA header word at offset 4, owned by the counted_array run row that sizes its property rows.
OMOD_PROPERTY_COUNT_KEY = "OMOD\\DATA@4"


def owner_from_tree(root, files, tokens, reason):
    # Constraint: ownership must be read from the tree, so a count cannot stay claimed after its owner is removed.
    texts = []
    for relative in files:
        path = Path(root) / relative
        if not path.is_file():
            return None
        texts.append(path.read_text(encoding="utf-8", errors="replace"))
    joined = "\n".join(texts)
    return reason if all(token in joined for token in tokens) else None


def vmad_fragment_owner(root):
    """Why the VMAD fragment count is modelled, or None when either shape loses its owner."""
    shapes = []
    for files, tokens, reason in (
            (VMAD_INDEXED_COUNT_FILES, VMAD_INDEXED_COUNT_TOKENS, VMAD_INDEXED_COUNT_REASON),
            (VMAD_FLAGGED_COUNT_FILES, VMAD_FLAGGED_COUNT_TOKENS, VMAD_FLAGGED_COUNT_REASON)):
        found = owner_from_tree(root, files, tokens, reason)
        if found is None:
            return None
        shapes.append(found)
    return "VMAD fragment counts: " + "; ".join(shapes)


def counter_rows(document):
    """Index every row or rule that declares ownership of a count subrecord."""
    owners = {}
    for record in document["records"]:
        signature = record.get("signature", "")
        for key in ("formlink_lists", "formlink_fields"):
            for row in record.get(key) or []:
                if row.get("counter"):
                    owners.setdefault(row["counter"], []).append(f"{signature}\\{row['path']}")
        for row in record.get("fields") or []:
            if row.get("kind") in ("counted_array", "subrecord_group") and row.get("count_subrecord"):
                owners.setdefault(row["count_subrecord"], []).append(row["path"])
            run = row.get("run") or {}
            if row.get("kind") == "counted_array" and row.get("subrecord") and "count_width" in run:
                owners.setdefault(f"{signature}\\{row['subrecord']}@{run['count_offset']}", []).append(
                    f"{row['path']} run count")
            if row.get("kind") == "subrecord_group" and run.get("count_member"):
                owners.setdefault(run["count_member"], []).append(f"{row['path']} run count")
        for row in record.get("subrecord_groups") or []:
            if row.get("count_subrecord"):
                owners.setdefault(row["count_subrecord"], []).append(row["path"])
        for rule in (record.get("authoring") or {}).get("subrecords") or []:
            if rule.get("coupled"):
                owners.setdefault(rule["signature"], []).append(f"{signature}\\{rule['signature']} (coupled)")
    return owners


def classify(entry, owners, root="."):
    if entry["model"] != "counted":
        return "unclassified", entry.get("reason", "")
    count = entry.get("count") or {}
    label = count.get("subrecord") or count.get("path") or ""
    if "FragmentCount" in label:
        owner = vmad_fragment_owner(root)
        if owner:
            return "mapped", owner
        return "unclassified", ("VMAD fragment count needs both its indexed count word and its "
                                "flag-keyed bits owned in this tree")
    if "Property Count" in label:
        # Constraint: the OMOD property count must stay owned by a schema run row, not by an inventory line.
        if owners.get(OMOD_PROPERTY_COUNT_KEY):
            return "mapped", ", ".join(sorted(set(owners[OMOD_PROPERTY_COUNT_KEY])))
        return "unclassified", "OMOD DATA property count has no run row in the schema"
    if count.get("kind") == "subrecord":
        signature = count["subrecord"]
        if owners.get(signature):
            return "mapped", ", ".join(sorted(set(owners[signature]))[:3])
        if signature in U8_COUNTERS:
            return "blocked", U8_COUNTERS[signature]
        if signature in VARIABLE_LENGTH_ELEMENTS:
            return "blocked", VARIABLE_LENGTH_ELEMENTS[signature]
        return "unclassified", "no Radicle row owns this counter"

    path = count.get("path", "")
    normalised = path.replace("\\\\", "\\").lstrip(".\\")
    if normalised.startswith("XCNT\\") and owners.get("XCNT"):
        return "mapped", ", ".join(sorted(set(owners["XCNT"]))[:3])
    if normalised.startswith("XRMR\\") and owners.get("XRMR"):
        return "mapped", ", ".join(sorted(set(owners["XRMR"]))[:3])
    if normalised in MODELLED_PATHS:
        return "mapped", MODELLED_PATHS[normalised]
    # Constraint: an OBTS count stays mapped only while its writer is in the tree, so removing the writer unclassifies it instead of leaving a line-number claim behind.
    if normalised in OBTS_COUNT_PATHS:
        owner = owner_from_tree(root, OBTS_COUNT_FILES, OBTS_COUNT_TOKENS, OBTS_COUNT_REASON)
        if owner:
            return "mapped", owner
        return "unclassified", "OBTS include and property counts have no writer in this tree"
    return "unclassified", "named path is not modelled"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--inventory", default="schema/fallout4/xedit_count_paths.json")
    parser.add_argument("--schema", default="schema/fallout4/fields.json")
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()

    entries = json.loads(Path(args.inventory).read_text(encoding="utf-8"))["entries"]
    root = Path(args.schema).resolve().parents[2]
    owners = counter_rows(json.loads(Path(args.schema).read_text(encoding="utf-8")))

    counts = {"mapped": 0, "blocked": 0, "unclassified": 0}
    problems = []
    for entry in entries:
        status, detail = classify(entry, owners, root)
        counts[status] += 1
        if status == "unclassified":
            problems.append((entry["line"], detail))
        label = (entry.get("count") or {}).get("subrecord") or (entry.get("count") or {}).get("path")
        print(f"{status:<13} {entry['line']:>6}  {label}")

    print()
    print(f"mapped {counts['mapped']}, blocked {counts['blocked']}, "
          f"unclassified {counts['unclassified']} of {len(entries)} count sites")
    for line, detail in problems:
        print(f"  UNCLASSIFIED line {line}: {detail}", file=sys.stderr)
    return 1 if args.strict and problems else 0


if __name__ == "__main__":
    sys.exit(main())
