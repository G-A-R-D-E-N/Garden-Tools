#!/usr/bin/env python3

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_address_codegen import parse_address, reject_overlaps, render_address  # noqa: E402
from schema_codegen import WIDTHS, load_fields, parse_subrecord_group  # noqa: E402

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


def parse_formlink_field_targets(row, path: str, union_targets):
    union_name = row.get("target_union")
    if union_name is not None:
        if not isinstance(union_name, str) or union_name not in union_targets:
            raise ValueError(f"unknown FormLink field target union for {path!r}: {union_name!r}")
        if "targets" in row:
            raise ValueError(f"FormLink field cannot declare targets and target_union: {path!r}")
        return list(union_targets[union_name])
    return parse_targets(row.get("targets"), path)


SEGMENT_KEYS = ("counter_payload_size", "segment_counters", "segment_index")


def parse_list_counter(row, path: str):
    counter = row.get("counter", "")
    if not isinstance(counter, str) or (counter and not valid_signature(counter)):
        raise ValueError(f"FormLink list counter is invalid: {path!r}")
    return counter


def parse_list_segment(row, path: str, storage: str, counter: str):
    present = [key for key in SEGMENT_KEYS if key in row]
    if not present:
        return None
    if len(present) != len(SEGMENT_KEYS):
        raise ValueError(f"FormLink list segment keys must be declared together: {path!r}")
    if storage != "packed_array":
        raise ValueError(f"FormLink list segments require packed_array storage: {path!r}")
    if not counter:
        raise ValueError(f"FormLink list segments require a counter subrecord: {path!r}")
    payload_size = row["counter_payload_size"]
    counters = row["segment_counters"]
    index = row["segment_index"]
    if type(payload_size) is not int or payload_size <= 0 or payload_size % 4 != 0 or payload_size > 0xFFFFFFFF:
        raise ValueError(f"FormLink list counter_payload_size must be a positive multiple of four: {path!r}")
    if not isinstance(counters, list) or not counters:
        raise ValueError(f"FormLink list segment_counters must be a non-empty array: {path!r}")
    if any(type(offset) is not int or offset < 0 or offset + 4 > payload_size for offset in counters):
        raise ValueError(f"FormLink list segment_counters must fit the counter payload: {path!r}")
    if len(set(counters)) != len(counters):
        raise ValueError(f"FormLink list segment_counters contains duplicate offsets: {path!r}")
    if type(index) is not int or not 0 <= index < len(counters):
        raise ValueError(f"FormLink list segment_index must index segment_counters: {path!r}")
    return {"payload_size": payload_size, "counters": list(counters), "index": index}


def parse_list_counter_offset(row, path: str, counter: str, segment):
    offset = row.get("counter_offset")
    if offset is None:
        return 0
    if type(offset) is not int or offset < 0 or offset > 0xFFFFFFFF:
        raise ValueError(f"FormLink list counter_offset must be a non-negative integer: {path!r}")
    if not counter:
        raise ValueError(f"FormLink list counter_offset requires a counter subrecord: {path!r}")
    if segment is not None:
        raise ValueError(f"FormLink list counter_offset cannot be combined with segments: {path!r}")
    return offset


def load_lists(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 field schema version")
    records = document.get("records")
    if not isinstance(records, list):
        raise ValueError("schema records must be an array")

    unions = load_target_unions(document)
    union_targets = {row["name"]: row["targets"] for row in unions}
    lists = []
    fields = []
    nested_lists = []
    seen_paths = set()
    seen_record_subrecords = {}
    segment_bodies = {}
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
            parsed_targets, wildcard_target = parse_targets_or_wildcard(row.get("targets"), path)
            if not isinstance(nullable, bool):
                raise ValueError(f"FormLink list nullable must be boolean: {path!r}")
            if mutation not in MUTATIONS:
                raise ValueError(f"unsupported FormLink list mutation policy: {mutation!r}")
            storage = row.get("storage", "per_subrecord")
            if storage not in ("per_subrecord", "packed_array"):
                raise ValueError(f"unsupported FormLink list storage: {storage!r}")
            counter = parse_list_counter(row, path)
            element_tail = row.get("element_tail", "")
            if not isinstance(element_tail, str) or (element_tail and not valid_signature(element_tail)):
                raise ValueError(f"FormLink list element_tail is invalid: {path!r}")
            if element_tail and storage != "per_subrecord":
                raise ValueError(f"FormLink list element_tail requires per_subrecord storage: {path!r}")
            segment = parse_list_segment(row, path, storage, counter)
            counter_offset = parse_list_counter_offset(row, path, counter, segment)
            key = (record_signature, subrecord)
            previous = seen_record_subrecords.get(key)
            if previous is not None and (segment is None or previous[1] is False):
                raise ValueError(
                    f"duplicate FormLink list subrecord mapping for {record_signature}.{subrecord}")
            seen_record_subrecords[key] = (path, segment is not None)
            if segment is not None:
                body = (segment["payload_size"], tuple(segment["counters"]))
                if segment_bodies.setdefault((record_signature, counter), body) != body:
                    raise ValueError(
                        f"FormLink list segments of {record_signature}.{counter} must share one "
                        f"counter layout: {path!r}")
            explicit_anchor = row.get("anchor_predecessors")
            if explicit_anchor is not None and (
                    not isinstance(explicit_anchor, list)
                    or not all(isinstance(value, str) and valid_signature(value) for value in explicit_anchor)):
                raise ValueError(f"FormLink list anchor_predecessors must be signatures: {path!r}")

            lists.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "field_alias": field_alias,
                "subrecord": subrecord,
                "storage": storage,
                "counter": counter,
                "counter_offset": counter_offset,
                "element_tail": element_tail,
                "segment": segment,
                "targets": parsed_targets,
                "nullable": nullable,
                "can_insert": mutation in ("existing_neighbors", "appendable"),
                "wildcard_target": wildcard_target,
                "append_when_empty": mutation == "appendable",
                "explicit_anchor": explicit_anchor,
            })

        field_rows = record.get("formlink_fields", [])
        if not isinstance(field_rows, list):
            raise ValueError(f"formlink_fields must be an array: {record_signature}")
        for group_field in record.get("fields", []):
            if isinstance(group_field, dict) and group_field.get("kind") == "subrecord_group":
                field_rows = field_rows + parse_subrecord_group(group_field, record_signature)[2]
        for row in field_rows:
            if not isinstance(row, dict):
                raise ValueError(f"FormLink field entries must be objects: {record_signature}")
            path = row.get("path", "")
            label = row.get("label", "")
            subrecord = row.get("subrecord", "")
            offset = row.get("offset")
            nullable = row.get("nullable")
            insertion = row.get("insertion")
            if not isinstance(path, str) or not path.startswith(record_signature + "\\"):
                raise ValueError(f"FormLink field path must be qualified by {record_signature}\\: {path!r}")
            if "[" in path or "]" in path or path in seen_paths:
                raise ValueError(f"invalid or duplicate FormLink field path: {path!r}")
            seen_paths.add(path)
            if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
                raise ValueError(f"FormLink field label is invalid: {label!r}")
            if not isinstance(subrecord, str) or not valid_signature(subrecord):
                raise ValueError(f"FormLink field subrecord is invalid: {path!r}")
            if isinstance(offset, bool) or not isinstance(offset, int) or offset < 0:
                raise ValueError(f"FormLink field offset must be a non-negative integer: {path!r}")
            if not isinstance(nullable, bool):
                raise ValueError(f"FormLink field nullable must be boolean: {path!r}")
            if insertion != "existing_only":
                raise ValueError(f"unsupported FormLink field insertion policy: {insertion!r}")
            payload_sizes = row.get("payload_sizes", [])
            if "payload_sizes" in row:
                if not isinstance(payload_sizes, list) or not 1 <= len(payload_sizes) <= 2:
                    raise ValueError(f"FormLink payload_sizes must contain one or two sizes: {path!r}")
                if any(type(size) is not int or size < offset + 4 or size > 0xFFFFFFFF
                       for size in payload_sizes):
                    raise ValueError(f"FormLink payload_sizes must contain fitting positive integers: {path!r}")
                if len(set(payload_sizes)) != len(payload_sizes):
                    raise ValueError(f"FormLink payload_sizes contains duplicate sizes: {path!r}")
            min_form_version = row.get("min_form_version", 0)
            if "min_form_version" in row and (type(min_form_version) is not int or not 0 < min_form_version <= 0xFFFF):
                raise ValueError(f"FormLink min_form_version must be a 16-bit positive integer: {path!r}")
            max_form_version = row.get("max_form_version", 0)
            if "max_form_version" in row and (type(max_form_version) is not int or not 0 < max_form_version <= 0xFFFF):
                raise ValueError(f"FormLink max_form_version must be a 16-bit positive integer: {path!r}")
            if max_form_version and max_form_version <= min_form_version:
                raise ValueError(f"FormLink max_form_version must exceed min_form_version: {path!r}")
            fields.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "subrecord": subrecord,
                "offset": offset,
                "nullable": nullable,
                "address": parse_address(row, path, subrecord, True, 4),
                "payload_sizes": payload_sizes,
                "max_form_version": max_form_version,
                "min_form_version": min_form_version,
                "targets": parse_formlink_field_targets(row, path, union_targets),
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
            if item["explicit_anchor"] is not None:
                item["anchor_predecessors"] = list(item["explicit_anchor"])
                item["anchor_successors"] = []
                continue
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
        seen_record_subrecords[subrecord_key] = (path, False)
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
            "counter_offset": 0,
            "element_tail": "",
            "segment": None,
            "targets": ["KYWD"],
            "nullable": False,
            "can_insert": True,
            "wildcard_target": False,
            "append_when_empty": False,
            "anchor_predecessors": anchors["predecessors"],
            "anchor_successors": anchors["successors"],
        })

    scalar_entries = [dict(field, width=WIDTHS.get(field["storage"], 0))
                      for field in load_fields(schema_path)
                      if field["address"]["segment"] is None]
    entries = [dict(field, width=4, embedded=True) for field in fields]
    for row in lists + nested_lists:
        # Constraint: a segmented list has no static slot range, so it is not overlap-checked.
        if row.get("segment") is not None:
            continue
        packed = row.get("storage") == "packed_array"
        entries.append({
            "record": row["record"],
            "subrecord": row["subrecord"],
            "path": row["path"] if "path" in row else row["container_path"] + "\\" + row["member"],
            "offset": 0,
            "width": 0,
            "embedded": False,
            "address": {"repeated": not packed, "stride": 4 if packed else 0, "array_offset": 0, "when": None},
        })
    reject_overlaps(entries, "FormLink")
    reject_overlaps(scalar_entries + entries, "FormLink/scalar")

    lists.sort(key=lambda item: (item["record"], item["path"]))
    nested_lists.sort(key=lambda item: (item["record"], item["container_path"], item["member"]))
    fields.sort(key=lambda item: (item["record"], item["path"]))
    return lists, nested_lists, fields, unions


def load_target_unions(document):
    rows = document.get("target_unions", [])
    if not isinstance(rows, list):
        raise ValueError("target_unions must be an array")
    unions = []
    seen = set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError("target_unions entries must be objects")
        name = row.get("name", "")
        if name != "sigBaseObjects":
            raise ValueError(f"unsupported target union: {name!r}")
        if name in seen:
            raise ValueError(f"duplicate target union: {name}")
        seen.add(name)
        targets = parse_targets(row.get("targets"), name)
        if "NULL" in targets:
            raise ValueError("sigBaseObjects omits NULL; it is not a record signature")
        unions.append({"name": name, "targets": targets})
    if "sigBaseObjects" not in seen:
        raise ValueError("target_unions must declare sigBaseObjects")
    return unions


GENERATED_HEADER = (
    "// Generated by Radicle/tools/formlink_schema_codegen.py from schema/fallout4/fields.json.",
    "// Do not hand-edit; update the JSON source and regenerate.",
)

# Constraint: shards are included in order inside one array so every index stays an offset.
SHARD_ENTRY_LIMIT = 600
SHARD_LINE_LIMIT = 750


def signature_entry(record: str, target: str):
    return record, f"  Signature::from_string({cpp_string(target)}),"


def chunk_entries(entries):
    groups = []
    for record, text in entries:
        if not groups or groups[-1][0] != record:
            groups.append((record, [text]))
        else:
            groups[-1][1].append(text)
    chunks = []
    current = []
    count = 0
    for group in groups:
        if current and count + len(group[1]) > SHARD_ENTRY_LIMIT:
            chunks.append(current)
            current = []
            count = 0
        current.append(group)
        count += len(group[1])
    if current:
        chunks.append(current)
    return chunks


def render_shard(name: str, index: int, total: int, chunk) -> str:
    first = chunk[0][0]
    last = chunk[-1][0]
    rows = sum(len(group_rows) for _record, group_rows in chunk)
    text = list(GENERATED_HEADER)
    text.append(f"// Shard {index + 1} of {total} of {name}: records {first} to {last}, {rows} entries.")
    text.extend(row for _record, group_rows in chunk for row in group_rows)
    if len(text) > SHARD_LINE_LIMIT:
        raise ValueError(f"generated FormLink schema shard exceeds {SHARD_LINE_LIMIT} lines: {name}")
    return "\n".join(text) + "\n"


def shard_files(output: Path):
    return sorted(output.parent.glob(f"{output.stem}_*_shard[0-9][0-9].inc"))


def render(lists, nested_lists, fields, unions, stem: str = "generated_formlink_lists"):
    shards = {}
    lines = list(GENERATED_HEADER)

    def emit_array(declaration: str, name: str, entries, placeholder=None) -> None:
        lines.append(f"inline constexpr {declaration} {name}[] = {{")
        if len(entries) > SHARD_ENTRY_LIMIT:
            chunks = chunk_entries(entries)
            for index, chunk in enumerate(chunks):
                shard = f"{stem}_{name.removeprefix('k_generated_')}_shard{index:02d}.inc"
                shards[shard] = render_shard(name, index, len(chunks), chunk)
                lines.append(f'#include "{shard}"')
        elif not entries:
            if placeholder is not None:
                lines.append(f"  {placeholder}")
        else:
            lines.extend(text for _record, text in entries)
        lines.append("};")
        lines.append("")

    list_target_entries = []
    descriptors = []
    target_start = 0
    for row in lists:
        for target in row["targets"]:
            list_target_entries.append(signature_entry(row["record"], target))
        descriptors.append((row, target_start, len(row["targets"])))
        target_start += len(row["targets"])
    nested_descriptors = []
    for row in nested_lists:
        for target in row["targets"]:
            list_target_entries.append(signature_entry(row["record"], target))
        nested_descriptors.append((row, target_start, len(row["targets"])))
        target_start += len(row["targets"])
    emit_array("Signature", "k_generated_formlink_list_targets", list_target_entries)

    field_target_entries = []
    field_descriptors = []
    field_target_start = 0
    for row in fields:
        for target in row["targets"]:
            field_target_entries.append(signature_entry(row["record"], target))
        field_descriptors.append((row, field_target_start, len(row["targets"])))
        field_target_start += len(row["targets"])
    emit_array("Signature", "k_generated_formlink_field_targets", field_target_entries, "Signature{},")

    # Sibling anchors used to position the first element of an otherwise-empty
    # flat list: each descriptor's preceding siblings (nearest first) followed by
    # its following siblings (nearest first).
    # Constraint: an empty constexpr array is ill-formed under -Wpedantic -Werror.
    anchor_entries = []
    anchor_meta = []
    anchor_start = 0
    for row, _first_target, _target_count in descriptors:
        predecessors = row.get("anchor_predecessors", [])
        successors = row.get("anchor_successors", [])
        for signature in predecessors:
            anchor_entries.append(signature_entry(row["record"], signature))
        for signature in successors:
            anchor_entries.append(signature_entry(row["record"], signature))
        anchor_meta.append((anchor_start, len(predecessors), len(successors)))
        anchor_start += len(predecessors) + len(successors)
    emit_array("Signature", "k_generated_formlink_list_anchors", anchor_entries, "Signature{},")

    # Counter byte offset in body order of the segment lists sharing one counter subrecord.
    segment_entries = []
    segment_start = 0
    segment_meta = []
    segment_bodies = {}
    for row, _first_target, _target_count in descriptors:
        segment = row.get("segment")
        if segment is None:
            segment_meta.append((0, 0, 0))
            continue
        body = (row["record"], row["counter"], tuple(segment["counters"]))
        if body not in segment_bodies:
            segment_bodies[body] = (segment_start, len(segment["counters"]))
            for offset in segment["counters"]:
                segment_entries.append((row["record"], f"  {offset},"))
            segment_start += len(segment["counters"])
        first_segment, segment_count = segment_bodies[body]
        segment_meta.append((segment["index"], first_segment, segment_count))
    emit_array("std::uint32_t", "k_generated_packed_segment_counters", segment_entries,
               "std::uint32_t{},")

    lines.append("inline constexpr FormLinkListDescriptor k_generated_formlink_lists[] = {")
    for (row, first_target, target_count), (first_anchor, predecessor_count, successor_count), (
            segment_index, first_segment, segment_count) in zip(
            descriptors, anchor_meta, segment_meta):
        segment = row.get("segment")
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
            f"{first_anchor}, {predecessor_count}, {successor_count}, " +
            f"{segment_index}, {first_segment}, {segment_count}, " +
            f"{segment['payload_size'] if segment else 0}, " +
            f"{row['counter_offset']}, " +
            (f"Signature::from_string({cpp_string(row['element_tail'])})"
                if row["element_tail"] else "Signature{}") +
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
    lines.append("")
    formlink_field_entries = []
    for row, first_target, target_count in field_descriptors:
        formlink_field_entries.append((row["record"],
            "  {" +
            f"Signature::from_string({cpp_string(row['record'])}), " +
            f"{cpp_string(row['path'])}, {cpp_string(row['label'])}, " +
            f"Signature::from_string({cpp_string(row['subrecord'])}), " +
            f"{row['offset']}, " +
            ("true" if row["nullable"] else "false") + ", " +
            f"{first_target}, {target_count}, " +
            render_address(row["address"]) +
            (", {{" + ", ".join(map(str, row["payload_sizes"])) + "}}"
             if row["payload_sizes"] or row["max_form_version"] or row["min_form_version"] else "") +
            (f", {row['max_form_version']}" if row["max_form_version"] or row["min_form_version"] else "") +
            (f", {row['min_form_version']}" if row["min_form_version"] else "") +
            "},"))
    emit_array("FormLinkFieldDescriptor", "k_generated_formlink_fields", formlink_field_entries)
    for union in unions:
        if union["name"] == "sigBaseObjects":
            emit_array("Signature", "k_generated_sig_base_objects",
                       [("", f"  Signature::from_string({cpp_string(target)}),")
                        for target in union["targets"]])
    return "\n".join(lines) + "\n", shards


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    lists, nested_lists, fields, unions = load_lists(args.schema)
    parent, shards = render(lists, nested_lists, fields, unions, args.output.stem)
    expected = {args.output: parent}
    for name, text in shards.items():
        expected[args.output.with_name(name)] = text
    orphans = [path for path in shard_files(args.output) if path not in expected]

    if args.check:
        ok = True
        for path, text in expected.items():
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                print(f"generated FormLink list schema is stale: {path}")
                ok = False
        for path in orphans:
            print(f"generated FormLink list schema has an orphan shard: {path}")
            ok = False
        return 0 if ok else 1

    for path, text in expected.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    for path in orphans:
        path.unlink()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
