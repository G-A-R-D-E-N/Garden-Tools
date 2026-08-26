#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

MUTATIONS = {"existing_only", "existing_neighbors", "appendable"}
NESTED_MUTATIONS = {"terminated_group"}


def parse_targets_or_wildcard(targets, path: str):
    # A wildcard list (FLST\Items accepts any major record) declares its targets
    # as "*"; it emits no target signatures and admits every record type.
    if targets == "*" or targets == ["*"]:
        return [], True
    return parse_targets(targets, path), False


def valid_signature(value: str) -> bool:
    return len(value) == 4 and all(0x20 <= ord(ch) <= 0x7E for ch in value)


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def parse_targets(targets, path: str):
    if not isinstance(targets, list) or not targets:
        raise ValueError(f"FormLink list targets must be a non-empty array: {path!r}")
    parsed = []
    for target in targets:
        if not isinstance(target, str) or not valid_signature(target):
            raise ValueError(f"invalid FormLink target signature for {path!r}: {target!r}")
        if target in parsed:
            raise ValueError(f"duplicate FormLink target signature for {path!r}: {target}")
        parsed.append(target)
    return parsed


def load_lists(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 field schema version")
    records = document.get("records")
    if not isinstance(records, list):
        raise ValueError("schema records must be an array")

    lists = []
    nested_lists = []
    seen_paths = set()
    seen_record_subrecords = set()
    seen_field_aliases = set()
    seen_nested_paths = set()
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("schema record entries must be objects")
        record_signature = record.get("signature", "")
        if not isinstance(record_signature, str) or not valid_signature(record_signature):
            raise ValueError(f"record signature must be four printable ASCII characters: {record_signature!r}")

        rows = record.get("formlink_lists", [])
        if not isinstance(rows, list):
            raise ValueError(f"formlink_lists must be an array: {record_signature}")
        for row in rows:
            if not isinstance(row, dict):
                raise ValueError(f"formlink list entries must be objects: {record_signature}")
            path = row.get("path", "")
            label = row.get("label", "")
            field_alias = row.get("field_alias", "")
            subrecord = row.get("subrecord", "")
            nullable = row.get("nullable")
            mutation = row.get("mutation")

            if not isinstance(path, str) or not path.startswith(record_signature + "\\"):
                raise ValueError(f"FormLink list path must be qualified by {record_signature}\\: {path!r}")
            if "[" in path or "]" in path:
                raise ValueError(f"FormLink list base path must not contain an index: {path!r}")
            if path in seen_paths:
                raise ValueError(f"duplicate FormLink list path: {path!r}")
            seen_paths.add(path)
            if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
                raise ValueError(f"FormLink list label is invalid: {label!r}")
            if not isinstance(subrecord, str) or not valid_signature(subrecord):
                raise ValueError(f"FormLink list subrecord is invalid: {subrecord!r}")
            if not isinstance(field_alias, str) or any(ord(ch) < 0x20 for ch in field_alias):
                raise ValueError(f"FormLink list field_alias is invalid: {field_alias!r}")
            if field_alias:
                alias_key = (record_signature, field_alias)
                if alias_key in seen_field_aliases:
                    raise ValueError(
                        f"duplicate FormLink list field_alias for {record_signature}.{field_alias}")
                seen_field_aliases.add(alias_key)
            key = (record_signature, subrecord)
            if key in seen_record_subrecords:
                raise ValueError(
                    f"duplicate FormLink list subrecord mapping for {record_signature}.{subrecord}")
            seen_record_subrecords.add(key)
            parsed_targets, wildcard_target = parse_targets_or_wildcard(row.get("targets"), path)
            if not isinstance(nullable, bool):
                raise ValueError(f"FormLink list nullable must be boolean: {path!r}")
            if mutation not in MUTATIONS:
                raise ValueError(f"unsupported FormLink list mutation policy: {mutation!r}")

            lists.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "field_alias": field_alias,
                "subrecord": subrecord,
                "storage": "per_subrecord",
                "counter": "",
                "targets": parsed_targets,
                "nullable": nullable,
                "can_insert": mutation in ("existing_neighbors", "appendable"),
                "wildcard_target": wildcard_target,
                "append_when_empty": mutation == "appendable",
            })

        nested_rows = record.get("nested_formlink_lists", [])
        if not isinstance(nested_rows, list):
            raise ValueError(f"nested_formlink_lists must be an array: {record_signature}")
        for row in nested_rows:
            if not isinstance(row, dict):
                raise ValueError(f"nested FormLink list entries must be objects: {record_signature}")
            container_path = row.get("container_path", "")
            member = row.get("member", "")
            label = row.get("label", "")
            subrecord = row.get("subrecord", "")
            terminator = row.get("terminator", "")
            terminator_size = row.get("terminator_size")
            nullable = row.get("nullable")
            mutation = row.get("mutation")
            path = f"{container_path}\\{member}"

            if not isinstance(container_path, str) or not container_path.startswith(record_signature + "\\"):
                raise ValueError(
                    f"nested FormLink container path must be qualified by {record_signature}\\: {container_path!r}")
            if "[" in container_path or "]" in container_path:
                raise ValueError(f"nested FormLink container base path must not contain an index: {container_path!r}")
            if not isinstance(member, str) or not member or "\\" in member or "[" in member or "]" in member:
                raise ValueError(f"nested FormLink member name is invalid: {member!r}")
            if path in seen_nested_paths or path in seen_paths:
                raise ValueError(f"duplicate nested FormLink list path: {path!r}")
            seen_nested_paths.add(path)
            if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
                raise ValueError(f"nested FormLink list label is invalid: {label!r}")
            if not isinstance(subrecord, str) or not valid_signature(subrecord):
                raise ValueError(f"nested FormLink list subrecord is invalid: {subrecord!r}")
            if not isinstance(terminator, str) or not valid_signature(terminator):
                raise ValueError(f"nested FormLink terminator is invalid: {terminator!r}")
            if isinstance(terminator_size, bool) or not isinstance(terminator_size, int) or terminator_size < 0:
                raise ValueError(f"nested FormLink terminator_size must be a non-negative integer: {path!r}")
            if terminator_size > 0xFFFF:
                raise ValueError(f"nested FormLink terminator_size exceeds u16 storage: {path!r}")
            parsed_targets = parse_targets(row.get("targets"), path)
            if not isinstance(nullable, bool):
                raise ValueError(f"nested FormLink list nullable must be boolean: {path!r}")
            if mutation not in NESTED_MUTATIONS:
                raise ValueError(f"unsupported nested FormLink mutation policy: {mutation!r}")

            nested_lists.append({
                "record": record_signature,
                "container_path": container_path,
                "member": member,
                "label": label,
                "subrecord": subrecord,
                "terminator": terminator,
                "terminator_size": terminator_size,
                "targets": parsed_targets,
                "nullable": nullable,
                "can_insert": mutation == "terminated_group",
            })

    # A flat list has no in-record terminator to anchor an insertion against, so
    # the first element of an otherwise-empty list is positioned relative to its
    # sibling lists using their declared (physical) order. Capture each list's
    # preceding and following siblings, nearest first, before the output sort
    # below discards that declaration order.
    record_groups = {}
    for item in lists:
        record_groups.setdefault(item["record"], []).append(item)
    for group in record_groups.values():
        for position, item in enumerate(group):
            item["anchor_predecessors"] = [
                group[j]["subrecord"] for j in range(position - 1, -1, -1)
            ]
            item["anchor_successors"] = [
                group[j]["subrecord"] for j in range(position + 1, len(group))
            ]

    keyword_records = document.get("keyword_records", [])
    if not isinstance(keyword_records, list):
        raise ValueError("keyword_records must be an array")
    seen_keyword_records = set()
    for entry in keyword_records:
        if not isinstance(entry, dict):
            raise ValueError("keyword_records entries must be objects")
        rec = entry.get("record", "")
        if not isinstance(rec, str) or not valid_signature(rec):
            raise ValueError(f"keyword record signature must be four printable ASCII characters: {rec!r}")
        if rec in seen_keyword_records:
            raise ValueError(f"duplicate keyword record: {rec}")
        seen_keyword_records.add(rec)
        path = rec + "\\Keywords"
        if path in seen_paths or path in seen_nested_paths:
            raise ValueError(f"keyword list path collides with an existing list: {path!r}")
        seen_paths.add(path)
        alias_key = (rec, "Keywords")
        if alias_key in seen_field_aliases:
            raise ValueError(f"keyword field_alias collides for {rec}.Keywords")
        seen_field_aliases.add(alias_key)
        subrecord_key = (rec, "KWDA")
        if subrecord_key in seen_record_subrecords:
            raise ValueError(f"keyword subrecord mapping collides for {rec}.KWDA")
        seen_record_subrecords.add(subrecord_key)
        anchors = {}
        for side in ("predecessors", "successors"):
            values = entry.get(side, [])
            if not isinstance(values, list):
                raise ValueError(f"keyword {side} must be an array: {rec}")
            for signature in values:
                if not isinstance(signature, str) or not valid_signature(signature):
                    raise ValueError(f"invalid keyword {side} signature for {rec}: {signature!r}")
            anchors[side] = list(values)
        lists.append({
            "record": rec,
            "path": path,
            "label": "Keywords",
            "field_alias": "Keywords",
            "subrecord": "KWDA",
            "storage": "packed_array",
            "counter": "KSIZ",
            "targets": ["KYWD"],
            "nullable": False,
            "can_insert": True,
            "wildcard_target": False,
            "append_when_empty": False,
            "anchor_predecessors": anchors["predecessors"],
            "anchor_successors": anchors["successors"],
        })

    lists.sort(key=lambda item: (item["record"], item["path"]))
    nested_lists.sort(key=lambda item: (item["record"], item["container_path"], item["member"]))
    return lists, nested_lists


def render(lists, nested_lists) -> str:
    lines = [
        "// Generated by Radicle/tools/formlink_schema_codegen.py from schema/fallout4/fields.json.",
        "// Do not hand-edit; update the JSON source and regenerate.",
        "inline constexpr Signature k_generated_formlink_list_targets[] = {",
    ]
    target_start = 0
    descriptors = []
    nested_descriptors = []
    for row in lists:
        for target in row["targets"]:
            lines.append(f"  Signature::from_string({cpp_string(target)}),")
        descriptors.append((row, target_start, len(row["targets"])))
        target_start += len(row["targets"])
    for row in nested_lists:
        for target in row["targets"]:
            lines.append(f"  Signature::from_string({cpp_string(target)}),")
        nested_descriptors.append((row, target_start, len(row["targets"])))
        target_start += len(row["targets"])

    lines.append("};")
    lines.append("")

    # Sibling anchors used to position the first element of an otherwise-empty
    # flat list: each descriptor's preceding siblings (nearest first) followed by
    # its following siblings (nearest first).
    lines.append("inline constexpr Signature k_generated_formlink_list_anchors[] = {")
    anchor_start = 0
    anchor_meta = []
    for row, _first_target, _target_count in descriptors:
        predecessors = row.get("anchor_predecessors", [])
        successors = row.get("anchor_successors", [])
        for signature in predecessors:
            lines.append(f"  Signature::from_string({cpp_string(signature)}),")
        for signature in successors:
            lines.append(f"  Signature::from_string({cpp_string(signature)}),")
        anchor_meta.append((anchor_start, len(predecessors), len(successors)))
        anchor_start += len(predecessors) + len(successors)
    if anchor_start == 0:
        # A zero-length constexpr array is ill-formed under -Wpedantic -Werror;
        # keep a never-indexed placeholder when no list declares a sibling.
        lines.append("  Signature{},")
    lines.append("};")
    lines.append("")

    lines.append("inline constexpr FormLinkListDescriptor k_generated_formlink_lists[] = {")
    for (row, first_target, target_count), (first_anchor, predecessor_count, successor_count) in zip(
            descriptors, anchor_meta):
        lines.append(
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), " +
            f"{cpp_string(row['path'])}, {cpp_string(row['label'])}, " +
            f"{cpp_string(row['field_alias'])}, " +
            f"Signature::from_string({cpp_string(row['subrecord'])}), " +
            f"FormLinkListStorage::{row['storage']}, " +
            (f"Signature::from_string({cpp_string(row['counter'])}), "
                if row["counter"] else "Signature{}, ") +
            ("true" if row["nullable"] else "false") + ", " +
            ("true" if row["can_insert"] else "false") + ", " +
            ("true" if row["wildcard_target"] else "false") + ", " +
            ("true" if row["append_when_empty"] else "false") + ", " +
            f"{first_target}, {target_count}, " +
            f"{first_anchor}, {predecessor_count}, {successor_count}" +
            "},"
        )
    lines.append("};")
    lines.append("")
    lines.append("inline constexpr NestedFormLinkListDescriptor k_generated_nested_formlink_lists[] = {")
    for row, first_target, target_count in nested_descriptors:
        lines.append(
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), " +
            f"{cpp_string(row['container_path'])}, {cpp_string(row['member'])}, " +
            f"{cpp_string(row['label'])}, " +
            f"Signature::from_string({cpp_string(row['subrecord'])}), " +
            f"Signature::from_string({cpp_string(row['terminator'])}), " +
            f"{row['terminator_size']}, " +
            ("true" if row["nullable"] else "false") + ", " +
            ("true" if row["can_insert"] else "false") + ", " +
            f"{first_target}, {target_count}" +
            "},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    lists, nested_lists = load_lists(args.schema)
    expected = render(lists, nested_lists)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != expected:
            print(f"generated FormLink list schema is stale: {args.output}")
            return 1
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
