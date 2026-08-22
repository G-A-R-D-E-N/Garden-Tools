#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

MUTATIONS = {"existing_only", "existing_neighbors"}
NESTED_MUTATIONS = {"terminated_group"}


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
            key = (record_signature, subrecord)
            if key in seen_record_subrecords:
                raise ValueError(
                    f"duplicate FormLink list subrecord mapping for {record_signature}.{subrecord}")
            seen_record_subrecords.add(key)
            parsed_targets = parse_targets(row.get("targets"), path)
            if not isinstance(nullable, bool):
                raise ValueError(f"FormLink list nullable must be boolean: {path!r}")
            if mutation not in MUTATIONS:
                raise ValueError(f"unsupported FormLink list mutation policy: {mutation!r}")

            lists.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "subrecord": subrecord,
                "targets": parsed_targets,
                "nullable": nullable,
                "can_insert": mutation == "existing_neighbors",
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
    lines.append("inline constexpr FormLinkListDescriptor k_generated_formlink_lists[] = {")
    for row, first_target, target_count in descriptors:
        lines.append(
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), " +
            f"{cpp_string(row['path'])}, {cpp_string(row['label'])}, " +
            f"Signature::from_string({cpp_string(row['subrecord'])}), " +
            ("true" if row["nullable"] else "false") + ", " +
            ("true" if row["can_insert"] else "false") + ", " +
            f"{first_target}, {target_count}" +
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
