#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_address_codegen import (cpp_signature, is_count, parse_address, parse_segment,  # noqa: E402
                                   reject_overlaps, render_address)

STORAGES = {"u8", "u16", "u32", "i32", "f32", "i8", "i16"}
KINDS = {"scalar", "string"}
STRING_FORBIDDEN = {"storage", "presentation", "minimum", "maximum", "stride", "array_offset", "names"}
PRESENTATIONS = {"identity", "percent_0_100", "enum", "flags"}
NAME_KEYS = {"enum": "value", "flags": "bit"}
CPP_PRESENTATIONS = {"enum": "enumeration"}
# Missing-field insertion is intentionally not generated yet. Safe insertion
# requires record-level subrecord ordering metadata, not only a scalar field row.
INSERTIONS = {"existing_only"}
COUNTED_ARRAY_KEYS = {"kind", "path", "label", "subrecord", "count_subrecord", "stride", "insertion",
                      "members", "count_word", "segment_index", "min_form_version"}
MEMBER_KEYS = {"path", "label", "offset", "storage", "presentation", "minimum", "maximum"}
SUBRECORD_GROUP_KEYS = {"kind", "path", "label", "count_subrecord", "insertion", "members", "successors",
                        "run", "requires_existing_count"}
GROUP_MEMBER_KEYS = {"subrecord", "required", "repeatable", "payload_size", "default", "fields",
                     "formlink_fields", "localized", "skippable", "leading"}
GROUP_RUN_KEYS = {"count_member", "members"}
GROUP_ROW_FORBIDDEN = {"subrecord", "repeated", "stride", "array_offset", "when", "payload_size", "minimum_payload_size"}
GROUP_LINK_KEYS = {"path", "label", "offset", "nullable", "insertion", "target_union", "targets"}
HEX_DIGITS = "0123456789abcdefABCDEF"
WIDTHS = {"u8": 1, "u16": 2, "u32": 4, "i32": 4, "f32": 4, "i8": 1, "i16": 2}
INTEGER_BOUNDS = {
    "i8": (-0x80, 0x7F),
    "i16": (-0x8000, 0x7FFF),
    "u8": (0, 0xFF),
    "u16": (0, 0xFFFF),
    "u32": (0, 0xFFFFFFFF),
    "i32": (-0x80000000, 0x7FFFFFFF),
}


def cpp_string(value: str) -> str:
    # JSON string escaping is valid for the ASCII field metadata currently
    # accepted here and avoids hand-written backslash/quote escaping bugs.
    return json.dumps(value, ensure_ascii=True)


def cpp_number(value) -> str:
    number = float(value)
    if number.is_integer():
        return f"{int(number)}.0"
    return repr(number)


def numeric(value, name: str, path: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{name} must be numeric for {path!r}")
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"{name} must be finite for {path!r}")
    return number


def valid_signature(value: str) -> bool:
    return len(value) == 4 and all(ord(ch) <= 0xFF for ch in value)


def parse_names(names, presentation: str, storage: str, minimum: float, maximum: float, path: str):
    key = NAME_KEYS.get(presentation)
    if key is None:
        if names is not None:
            raise ValueError(f"names require the enum or flags presentation: {path!r}")
        return []
    if storage not in INTEGER_BOUNDS:
        raise ValueError(f"{presentation} presentation requires integer storage: {path!r}")
    low, high = INTEGER_BOUNDS[storage]
    if presentation == "flags" and low < 0:
        raise ValueError(f"flags presentation requires unsigned storage: {path!r}")
    if not isinstance(names, list) or not names:
        raise ValueError(f"{presentation} presentation requires a non-empty names array: {path!r}")

    parsed = []
    seen_names = set()
    seen_numbers = set()
    for entry in names:
        if not isinstance(entry, dict) or set(entry) != {key, "name"}:
            raise ValueError(f"{presentation} names entries need exactly {key!r} and 'name': {path!r}")
        number, name = entry[key], entry["name"]
        if type(number) is not int:
            raise ValueError(f"names {key} must be an integer: {path!r}")
        if not isinstance(name, str) or not name or not all(0x20 <= ord(ch) <= 0x7E for ch in name):
            raise ValueError(f"names name must be non-empty printable ASCII: {path!r}")
        if name in seen_names:
            raise ValueError(f"duplicate name {name!r}: {path!r}")
        if number in seen_numbers:
            raise ValueError(f"duplicate {key} {number}: {path!r}")
        seen_names.add(name)
        seen_numbers.add(number)

        if presentation == "flags":
            if not 0 <= number < 8 * WIDTHS[storage]:
                raise ValueError(f"flag bit {number} exceeds {storage} storage: {path!r}")
            value = 1 << number
        else:
            if not low <= number <= high:
                raise ValueError(f"enum value {number} exceeds {storage} storage: {path!r}")
            value = number
        if not minimum <= value <= maximum:
            raise ValueError(f"{presentation} {key} {number} is outside the field bounds: {path!r}")
        parsed.append({"value": value, "name": name})
    return parsed


def parse_counted_array(field, record_signature: str):
    path = field.get("path", "")
    unknown = sorted(set(field) - COUNTED_ARRAY_KEYS)
    if unknown:
        raise ValueError(f"unknown counted_array keys {unknown}: {path!r}")
    if not isinstance(path, str) or not path.startswith(record_signature + "\\"):
        raise ValueError(f"counted_array path must be qualified by {record_signature}\\: {path!r}")
    label = field.get("label", "")
    if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
        raise ValueError(f"counted_array label is invalid: {label!r}")
    subrecord = field.get("subrecord", "")
    count_subrecord = field.get("count_subrecord", "")
    signatures = (subrecord, count_subrecord) if "count_subrecord" in field else (subrecord,)
    for signature in signatures:
        if not isinstance(signature, str) or not valid_signature(signature):
            raise ValueError(f"counted_array signatures must be four printable ASCII characters: {signature!r}: {path!r}")
    if count_subrecord == subrecord:
        raise ValueError(f"counted_array count_subrecord must differ from its subrecord: {path!r}")
    stride = field.get("stride")
    if not is_count(stride, 1):
        raise ValueError(f"counted_array stride must be a positive integer: {path!r}")
    if field.get("insertion") not in INSERTIONS:
        raise ValueError(f"unsupported insertion policy until record ordering exists: {field.get('insertion')!r}")
    min_form_version = field.get("min_form_version", 0)
    if "min_form_version" in field and (type(min_form_version) is not int or not 0 < min_form_version <= 0xFFFF):
        raise ValueError(f"counted_array min_form_version must be a 16-bit positive integer: {path!r}")
    segment = parse_segment(field, path, stride, False)
    if segment is not None and "count_subrecord" in field:
        raise ValueError(f"a header-counted segment reads its count from the payload header: {path!r}")
    members = field.get("members")
    if not isinstance(members, list) or not members:
        raise ValueError(f"counted_array members must be a non-empty array: {path!r}")
    rows = []
    for member in members:
        if not isinstance(member, dict) or set(member) - MEMBER_KEYS:
            raise ValueError(f"counted_array member keys must be a subset of {sorted(MEMBER_KEYS)}: {path!r}")
        row = dict(member, kind="scalar", subrecord=subrecord, stride=stride, insertion="existing_only")
        if segment is not None:
            row["count_word"] = segment["count_word"]
            row["segment_index"] = segment["index"]
        if min_form_version:
            row["min_form_version"] = min_form_version
        rows.append(row)
    array = {"record": record_signature, "path": path, "label": label, "subrecord": subrecord,
             "count_subrecord": count_subrecord, "stride": stride, "segment": segment}
    return array, rows


def parse_group_default(value, payload_size: int, path: str):
    if not isinstance(value, str) or not value or len(value) % 2 or any(ch not in HEX_DIGITS for ch in value):
        raise ValueError(f"subrecord_group member default must be an even-length hex string: {path!r}")
    data = bytes.fromhex(value)
    if len(data) != payload_size and payload_size:
        raise ValueError(f"subrecord_group member default must be {payload_size} bytes: {path!r}")
    return list(data)


def parse_group_members(members, path: str, reserved: set):
    parsed = []
    rows = []
    links = []
    signatures = set()
    for member in members:
        if not isinstance(member, dict) or set(member) - GROUP_MEMBER_KEYS:
            raise ValueError(f"subrecord_group member keys must be a subset of {sorted(GROUP_MEMBER_KEYS)}: {path!r}")
        signature = member.get("subrecord", "")
        if not isinstance(signature, str) or not valid_signature(signature):
            raise ValueError(f"subrecord_group member signature must be four printable ASCII characters: {signature!r}: {path!r}")
        if signature in signatures or signature in reserved:
            raise ValueError(f"subrecord_group member {signature} is repeated or is the count subrecord: {path!r}")
        signatures.add(signature)
        required = member.get("required")
        repeatable = member.get("repeatable", False)
        if not isinstance(required, bool) or not isinstance(repeatable, bool):
            raise ValueError(f"subrecord_group member required and repeatable must be booleans: {path!r}")
        payload_size = member.get("payload_size")
        if not is_count(payload_size, 0):
            raise ValueError(f"subrecord_group member payload_size must be a non-negative integer: {path!r}")
        localized = member.get("localized", False)
        if not isinstance(localized, bool):
            raise ValueError(f"subrecord_group member localized must be a boolean: {path!r}")
        skippable = member.get("skippable", False)
        if not isinstance(skippable, bool):
            raise ValueError(f"subrecord_group member skippable must be a boolean: {path!r}")
        leading = member.get("leading", False)
        if not isinstance(leading, bool):
            raise ValueError(f"subrecord_group member leading must be a boolean: {path!r}")
        if leading and (required or repeatable or skippable or any(not entry["leading"] for entry in parsed)):
            raise ValueError(f"subrecord_group leading members must come first and be optional, non-repeatable and not skippable: {path!r}")
        default = parse_group_default(member["default"], payload_size, path) if "default" in member else None
        parsed.append({"subrecord": signature, "required": required, "repeatable": repeatable,
                       "payload_size": payload_size, "default": default, "localized": localized,
                       "skippable": skippable, "leading": leading})
        member_fields = member.get("fields", [])
        member_links = member.get("formlink_fields", [])
        if not isinstance(member_fields, list) or not isinstance(member_links, list):
            raise ValueError(f"subrecord_group member rows must be arrays: {path!r}")
        if (member_fields or member_links) and (not required or repeatable):
            raise ValueError(f"subrecord_group rows need a required, non-repeatable member: {path!r}")
        for row in member_fields:
            if not isinstance(row, dict) or set(row) & GROUP_ROW_FORBIDDEN:
                raise ValueError(f"subrecord_group member field keys must not include {sorted(GROUP_ROW_FORBIDDEN)}: {path!r}")
            kind = row.get("kind", "scalar")
            if kind not in KINDS:
                raise ValueError(f"subrecord_group member field kind is unsupported: {row.get('path')!r}")
            if kind == "string":
                forbidden = sorted(STRING_FORBIDDEN & set(row))
                if forbidden:
                    raise ValueError(f"subrecord_group member string rows do not accept {forbidden}: {row.get('path')!r}")
                if not is_count(row.get("offset"), 0):
                    raise ValueError(f"subrecord_group member string row needs a non-negative offset: {row.get('path')!r}")
                rows.append(dict(row, kind="string", subrecord=signature, repeated=True, group_member=True))
                continue
            width = WIDTHS.get(row.get("storage"))
            if width is None:
                raise ValueError(f"subrecord_group member field storage is unsupported: {row.get('path')!r}")
            if not is_count(row.get("offset", 0), 0):
                raise ValueError(f"subrecord_group member field position is invalid: {row.get('path')!r}")
            end = row.get("offset", 0) + width
            if end > payload_size or ("offset" not in row and end != payload_size):
                raise ValueError(f"subrecord_group member field exceeds member size: {row.get('path')!r}")
            rows.append(dict(row, kind="scalar", subrecord=signature, repeated=True, group_member=True))
        for row in member_links:
            if not isinstance(row, dict) or set(row) - GROUP_LINK_KEYS:
                raise ValueError(f"subrecord_group member link keys must be a subset of {sorted(GROUP_LINK_KEYS)}: {path!r}")
            if not is_count(row.get("offset"), 0):
                raise ValueError(f"subrecord_group member link position is invalid: {row.get('path')!r}")
            if row["offset"] + WIDTHS["u32"] > payload_size:
                raise ValueError(f"subrecord_group member link exceeds member size: {row.get('path')!r}")
            links.append(dict(row, subrecord=signature, repeated=True))
    return parsed, rows, links, signatures


def parse_subrecord_group(field, record_signature: str):
    path = field.get("path", "")
    unknown = sorted(set(field) - SUBRECORD_GROUP_KEYS)
    if unknown:
        raise ValueError(f"unknown subrecord_group keys {unknown}: {path!r}")
    if not isinstance(path, str) or not path.startswith(record_signature + "\\"):
        raise ValueError(f"subrecord_group path must be qualified by {record_signature}\\: {path!r}")
    label = field.get("label", "")
    if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
        raise ValueError(f"subrecord_group label is invalid: {label!r}")
    count_subrecord = field.get("count_subrecord", "")
    if not isinstance(count_subrecord, str) or (count_subrecord and not valid_signature(count_subrecord)):
        raise ValueError(f"subrecord_group count_subrecord must be four printable ASCII characters: {path!r}")
    if field.get("insertion") not in INSERTIONS:
        raise ValueError(f"unsupported insertion policy until record ordering exists: {field.get('insertion')!r}")
    requires_existing_count = field.get("requires_existing_count", False)
    if not isinstance(requires_existing_count, bool) or (requires_existing_count and not count_subrecord):
        raise ValueError(f"subrecord_group requires_existing_count must be a boolean on a counted group: {path!r}")
    members = field.get("members")
    if not isinstance(members, list) or not members:
        raise ValueError(f"subrecord_group members must be a non-empty array: {path!r}")
    successors = field.get("successors", [])
    if (not isinstance(successors, list)
            or not all(isinstance(entry, str) and valid_signature(entry) for entry in successors)
            or len(set(successors)) != len(successors)):
        raise ValueError(f"subrecord_group successors must be distinct four-character signatures: {path!r}")

    parsed, rows, links, signatures = parse_group_members(
        members, path, {count_subrecord} if count_subrecord else set())
    parsed_run = None
    if "run" in field:
        run = field["run"]
        if not isinstance(run, dict) or set(run) != GROUP_RUN_KEYS:
            raise ValueError(f"subrecord_group run keys must be exactly {sorted(GROUP_RUN_KEYS)}: {path!r}")
        count_member = run.get("count_member", "")
        if not isinstance(count_member, str) or not valid_signature(count_member):
            raise ValueError(f"subrecord_group run count_member must be four printable ASCII characters: {path!r}")
        holder = next((member for member in parsed if member["subrecord"] == count_member), None)
        if holder is None or holder["payload_size"] != WIDTHS["u32"] or holder["repeatable"]:
            raise ValueError(f"subrecord_group run count_member must be a four-byte non-repeatable member: {path!r}")
        run_members = run.get("members")
        if not isinstance(run_members, list) or not run_members:
            raise ValueError(f"subrecord_group run members must be a non-empty array: {path!r}")
        if any(isinstance(member, dict) and member.get("leading") for member in run_members):
            raise ValueError(f"subrecord_group run members cannot be leading: {path!r}")
        nested, nested_rows, nested_links, _nested_signatures = parse_group_members(
            run_members, path, signatures | set(successors)
            | ({count_subrecord} if count_subrecord else set()))
        if not nested[0]["required"] or nested[0]["repeatable"]:
            raise ValueError(f"subrecord_group run leader must be required and non-repeatable: {path!r}")
        rows.extend(nested_rows)
        links.extend(nested_links)
        parsed_run = {"count_member": count_member, "members": nested}
    leader = next((entry for entry in parsed if not entry["leading"]), None)
    if leader is None or not leader["required"] or leader["repeatable"]:
        raise ValueError(f"subrecord_group leader must be required and non-repeatable: {path!r}")
    if set(successors) & (signatures | {count_subrecord}):
        raise ValueError(f"subrecord_group successors cannot be members or the count subrecord: {path!r}")
    group = {"record": record_signature, "path": path, "label": label,
             "count_subrecord": count_subrecord, "members": parsed, "successors": successors,
             "run": parsed_run, "requires_existing_count": requires_existing_count}
    return group, rows, links


def whole_payload_u32(row) -> bool:
    address = row["address"]
    return (not row["embedded"] and row["storage"] == "u32" and not row["payload_size"]
            and not row["minimum_payload_size"] and not address["repeated"] and not address["stride"]
            and address["when"] is None)


def load_fields(schema_path: Path):
    return load_schema(schema_path)[0]


def load_schema(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 field schema version")

    records = document.get("records")
    if not isinstance(records, list):
        raise ValueError("schema records must be an array")

    fields = []
    arrays = []
    groups = []
    seen_paths = set()
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("schema record entries must be objects")
        record_signature = record.get("signature", "")
        if not isinstance(record_signature, str) or not valid_signature(record_signature):
            raise ValueError(f"record signature must be four printable ASCII characters: {record_signature!r}")

        record_fields = record.get("fields")
        if not isinstance(record_fields, list):
            raise ValueError(f"record fields must be an array: {record_signature}")

        expanded = []
        for field in record_fields:
            if isinstance(field, dict) and field.get("kind") == "counted_array":
                array, members = parse_counted_array(field, record_signature)
                if array["path"] in seen_paths:
                    raise ValueError(f"duplicate schema field path: {array['path']!r}")
                seen_paths.add(array["path"])
                arrays.append(array)
                expanded.extend(members)
            elif isinstance(field, dict) and field.get("kind") == "subrecord_group":
                group, members, _links = parse_subrecord_group(field, record_signature)
                if group["path"] in seen_paths:
                    raise ValueError(f"duplicate schema field path: {group['path']!r}")
                seen_paths.add(group["path"])
                groups.append(group)
                expanded.extend(members)
            else:
                expanded.append(field)

        for field in expanded:
            if not isinstance(field, dict):
                raise ValueError(f"field entries must be objects: {record_signature}")
            kind = field.get("kind")
            if kind not in KINDS:
                raise ValueError(f"unsupported field kind: {kind!r}")
            is_string = kind == "string"
            if is_string:
                forbidden = sorted(STRING_FORBIDDEN & set(field))
                if forbidden:
                    raise ValueError(f"string fields do not accept {forbidden}: {field.get('path')!r}")

            path = field.get("path", "")
            label = field.get("label", "")
            if not isinstance(path, str) or not path:
                raise ValueError("schema field path is required")
            if not path.startswith(record_signature + "\\"):
                raise ValueError(
                    f"field path must be qualified by {record_signature}\\: {path!r}")
            if path in seen_paths:
                raise ValueError(f"duplicate schema field path: {path!r}")
            seen_paths.add(path)
            if not isinstance(label, str) or not label or any(ord(ch) < 0x20 for ch in label):
                raise ValueError(f"schema field label is invalid: {label!r}")

            localized = field.get("localized", False)
            if type(localized) is not bool or (localized and not is_string):
                raise ValueError(f"localized must be a boolean on string rows only: {path!r}")

            subrecord = field.get("subrecord", "")
            if not isinstance(subrecord, str) or not valid_signature(subrecord):
                raise ValueError(f"subrecord signature must be four printable ASCII characters: {subrecord!r}")
            offset = field.get("offset")
            embedded = offset is not None
            if embedded:
                if not isinstance(offset, int) or isinstance(offset, bool) or offset < 0:
                    raise ValueError(f"scalar offset must be a non-negative integer: {path!r}")
            else:
                offset = 0

            if is_string:
                storage = "fixed_string" if "payload_size" in field else "zstring"
                presentation = "identity"
            else:
                storage = field.get("storage")
                presentation = field.get("presentation")
            insertion = field.get("insertion")
            if storage not in STORAGES and not is_string:
                raise ValueError(f"unsupported scalar storage: {storage!r}")

            address = parse_address(field, path, subrecord, embedded, WIDTHS.get(storage, 0))
            if presentation not in PRESENTATIONS:
                raise ValueError(f"unsupported scalar presentation: {presentation!r}")
            if insertion not in INSERTIONS:
                raise ValueError(
                    f"unsupported insertion policy until record ordering exists: {insertion!r}")

            payload_size = 0
            if "payload_size" in field:
                payload_size = field["payload_size"]
                if type(payload_size) is not int or payload_size <= 0:
                    raise ValueError(f"payload_size must be a positive integer: {path!r}")
                if is_string:
                    if embedded:
                        raise ValueError(
                            f"fixed-width string payload_size must cover the whole payload: {path!r}")
                else:
                    if not embedded:
                        raise ValueError(f"payload_size requires an embedded field: {path!r}")
                    if payload_size < offset + WIDTHS.get(storage, 0):
                        raise ValueError(f"payload_size is smaller than the field end: {path!r}")
                    if address["stride"] and (payload_size - address["array_offset"]) % address["stride"] != 0:
                        raise ValueError(f"payload_size does not hold whole stride elements: {path!r}")

            minimum_payload_size = field.get("minimum_payload_size", 0)
            if "minimum_payload_size" in field:
                if not embedded:
                    raise ValueError(f"minimum_payload_size requires an embedded field: {path!r}")
                if type(minimum_payload_size) is not int or minimum_payload_size <= 0:
                    raise ValueError(f"minimum_payload_size must be a positive integer: {path!r}")
                if minimum_payload_size < offset + WIDTHS.get(storage, 0):
                    raise ValueError(f"minimum_payload_size is smaller than the field end: {path!r}")
                if payload_size and minimum_payload_size > payload_size:
                    raise ValueError(f"minimum_payload_size exceeds payload_size: {path!r}")

            min_form_version = field.get("min_form_version", 0)
            if "min_form_version" in field:
                if type(min_form_version) is not int or not 0 < min_form_version <= 0xFFFF:
                    raise ValueError(f"min_form_version must be a 16-bit positive integer: {path!r}")
            max_form_version = field.get("max_form_version", 0)
            if "max_form_version" in field:
                if type(max_form_version) is not int or not 0 < max_form_version <= 0xFFFF:
                    raise ValueError(f"max_form_version must be a 16-bit positive integer: {path!r}")
                if max_form_version <= min_form_version:
                    raise ValueError(f"max_form_version must exceed min_form_version: {path!r}")

            if is_string:
                minimum = maximum = 0.0
            else:
                minimum = numeric(field.get("minimum"), "minimum", path)
                maximum = numeric(field.get("maximum"), "maximum", path)
            if minimum > maximum:
                raise ValueError(f"invalid scalar bounds for {path!r}")

            if storage in INTEGER_BOUNDS:
                low, high = INTEGER_BOUNDS[storage]
                if not minimum.is_integer() or not maximum.is_integer():
                    raise ValueError(f"integer storage requires integer bounds for {path!r}")
                if minimum < low or maximum > high:
                    raise ValueError(
                        f"schema bounds exceed {storage} storage for {path!r}")

            if presentation == "percent_0_100" and (minimum < 0 or maximum > 100):
                raise ValueError(
                    f"percent_0_100 bounds must stay within 0..100 for {path!r}")

            names = parse_names(field.get("names"), presentation, storage, minimum, maximum, path)

            fields.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "subrecord": subrecord,
                "storage": storage,
                "presentation": presentation,
                "names": names,
                "minimum": minimum,
                "maximum": maximum,
                "offset": offset,
                "embedded": embedded,
                "payload_size": payload_size,
                "minimum_payload_size": minimum_payload_size,
                "min_form_version": min_form_version,
                "max_form_version": max_form_version,
                "address": address,
                "localized": localized,
                "can_insert": False,
                "group_member": field.get("group_member", False),
            })

    for array in arrays:
        twins = [other for other in arrays
                 if (other["record"], other["subrecord"]) == (array["record"], array["subrecord"])]
        if len(twins) > 1 and not all(other["segment"] is not None for other in twins):
            raise ValueError(f"counted_array subrecord {array['subrecord']} is declared twice in {array['record']}: {array['path']!r}")
        if array["segment"] is not None or not array["count_subrecord"]:
            continue
        sharers = [other for other in arrays
                   if (other["record"], other["count_subrecord"]) == (array["record"], array["count_subrecord"])]
        if len(sharers) > 1:
            raise ValueError(f"counted_array count subrecord {array['count_subrecord']} is shared by two arrays in {array['record']}: {array['path']!r}")
        counts = [row for row in fields
                  if row["record"] == array["record"] and row["subrecord"] == array["count_subrecord"]]
        if len(counts) != 1 or not whole_payload_u32(counts[0]):
            raise ValueError(f"counted_array {array['path']!r} needs exactly one whole-payload u32 row for {array['count_subrecord']}")

    payload_segments = {}
    for array in arrays:
        if array["segment"] is None:
            continue
        payload_segments.setdefault((array["record"], array["subrecord"]), []).append(array)
    for (record_signature, subrecord), group in payload_segments.items():
        indexes = sorted(array["segment"]["index"] for array in group)
        if indexes != list(range(len(group))):
            raise ValueError(f"counted_array segments of {record_signature}.{subrecord} must be indexed 0..{len(group) - 1}: {group[0]['path']!r}")
        counters = [array["segment"]["count_word"] for array in group]
        if len(set(counters)) != len(counters):
            raise ValueError(f"counted_array segments of {record_signature}.{subrecord} must read different header counters: {group[0]['path']!r}")
        for array in group:
            array["segment"]["count"] = len(group)
        for field in fields:
            if (field["record"] == record_signature and field["subrecord"] == subrecord
                    and field["address"]["segment"] is not None):
                field["address"]["segment"]["count"] = len(group)

    for group in groups:
        if group["count_subrecord"]:
            counts = [row for row in fields
                      if row["record"] == group["record"] and row["subrecord"] == group["count_subrecord"]]
            if len(counts) != 1 or not whole_payload_u32(counts[0]):
                raise ValueError(f"subrecord_group {group['path']!r} needs exactly one whole-payload u32 row for {group['count_subrecord']}")
        group_signatures = {member["subrecord"] for member in group["members"]}
        if group["run"] is not None:
            group_signatures |= {member["subrecord"] for member in group["run"]["members"]}
        group_members_only = set(group_signatures)
        if group["count_subrecord"]:
            group_signatures.add(group["count_subrecord"])
        for array in arrays:
            if array["record"] == group["record"] and group_signatures & {array["subrecord"], array["count_subrecord"]}:
                raise ValueError(f"subrecord_group {group['path']!r} shares a subrecord with counted_array {array['path']!r} in {group['record']}")
        for other in groups:
            if other is group or other["record"] != group["record"]:
                continue
            other_members = {member["subrecord"] for member in other["members"]}
            if other["run"] is not None:
                other_members |= {member["subrecord"] for member in other["run"]["members"]}
            shared = sorted(group_members_only & other_members)
            if shared:
                raise ValueError(f"subrecord_group members {shared} are declared by two groups in {group['record']}: {group['path']!r}")
            if group["count_subrecord"] in other_members:
                raise ValueError(f"subrecord_group count subrecord {group['count_subrecord']} is a member of another group in {group['record']}: {group['path']!r}")
            if group["count_subrecord"] and other["count_subrecord"] == group["count_subrecord"]:
                raise ValueError(f"subrecord_group count subrecord {group['count_subrecord']} is shared by two groups in {group['record']}: {group['path']!r}")

    # Constraint: a header-counted segment has no static slot range, so it is not overlap-checked.
    reject_overlaps([dict(field, width=WIDTHS.get(field["storage"], 0)) for field in fields
                     if field["address"]["segment"] is None], "scalar")

    fields.sort(key=lambda item: (item["record"], item["path"]))
    arrays.sort(key=lambda item: (item["record"], item["path"]))
    groups.sort(key=lambda item: (item["record"], item["path"]))
    return fields, arrays, groups


def names_key(field):
    return tuple((entry["value"], entry["name"]) for entry in field["names"])


def render_group_members(members, lines, defaults):
    specs = []
    for member in members:
        spec = (f"{cpp_signature(member['subrecord'])}, "
                f"{'true' if member['required'] else 'false'}, "
                f"{'true' if member['repeatable'] else 'false'}, {member['payload_size']}, ")
        if member["default"] is not None:
            name = f"k_subrecord_group_default_{defaults[0]}"
            defaults[0] += 1
            body = ", ".join(f"0x{byte:02X}" for byte in member["default"])
            lines.append(f"constexpr std::uint8_t {name}[] = {{{body}}};")
            spec += f"std::span<const std::uint8_t>({name})"
        else:
            spec += "std::span<const std::uint8_t>{}"
        spec += f", {'true' if member['localized'] else 'false'}"
        spec += f", {'true' if member['skippable'] else 'false'}"
        spec += f", {'true' if member['leading'] else 'false'}"
        specs.append("{" + spec + "},")
    return specs


def render_subrecord_groups(groups):
    lines = []
    entries = []
    defaults = [0]
    for index, group in enumerate(groups):
        members = render_group_members(group["members"], lines, defaults)
        if group["successors"]:
            body = ", ".join(f"Signature::from_string({cpp_string(signature)})" for signature in group["successors"])
            lines.append(f"constexpr Signature k_subrecord_group_successors_{index}[] = {{{body}}};")
            successors = f"std::span<const Signature>(k_subrecord_group_successors_{index})"
        else:
            successors = "std::span<const Signature>{}"
        lines.append(f"constexpr SubrecordGroupMemberDescriptor k_subrecord_group_members_{index}[] = {{")
        lines.extend(members)
        lines.append("};")
        if group["run"] is None:
            run = "SubrecordGroupRunDescriptor{}"
        else:
            run_members = render_group_members(group["run"]["members"], lines, defaults)
            lines.append(f"constexpr SubrecordGroupMemberDescriptor k_subrecord_group_run_members_{index}[] = {{")
            lines.extend(run_members)
            lines.append("};")
            run = ("SubrecordGroupRunDescriptor{Signature::from_string(" +
                   cpp_string(group["run"]["count_member"]) + "), " +
                   f"std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_run_members_{index})" +
                   "}")
        entries.append(
            "{" +
            f"Signature::from_string({cpp_string(group['record'])}), " +
            f"{cpp_string(group['path'])}, " +
            f"{cpp_string(group['label'])}, " +
            (f"Signature::from_string({cpp_string(group['count_subrecord'])})"
             if group["count_subrecord"] else "Signature{}") + ", " +
            f"std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_members_{index}), " +
            successors + ", " +
            run + ", " +
            ('true' if group["requires_existing_count"] else 'false') +
            "},")
    if groups:
        lines.append(f"constexpr std::array<SubrecordGroupDescriptor, {len(groups)}> k_subrecord_groups{{{{")
        lines.extend(entries)
        lines.append("}};")
    else:
        lines.append("constexpr std::array<SubrecordGroupDescriptor, 0> k_subrecord_groups{};")
    return lines


def render(fields, arrays=(), groups=()) -> str:
    tables = {}
    for field in fields:
        if field["names"]:
            tables.setdefault(names_key(field), len(tables))

    lines = [
        "// Generated by Radicle/tools/schema_codegen.py from schema/fallout4/fields.json.",
        "// Do not hand-edit; update the JSON source and regenerate.",
    ]
    for entries, index in tables.items():
        body = ", ".join("{" + f"{value}, {cpp_string(name)}" + "}" for value, name in entries)
        lines.append(f"constexpr ScalarName k_scalar_names_{index}[] = {{{body}}};")
    lines.append("constexpr ScalarFieldDescriptor k_scalar_fields[] = {")
    for field in fields:
        trailing = ""
        if field["names"]:
            trailing = (f", {field['minimum_payload_size']}, "
                        f"std::span<const ScalarName>(k_scalar_names_{tables[names_key(field)]})")
        elif field["min_form_version"]:
            trailing = f", {field['minimum_payload_size']}, {{}}"
        elif field["minimum_payload_size"]:
            trailing = f", {field['minimum_payload_size']}"
        if field["min_form_version"]:
            trailing += f", {field['min_form_version']}"
        if field["localized"] or field["max_form_version"]:
            names = (f"std::span<const ScalarName>(k_scalar_names_{tables[names_key(field)]})"
                     if field["names"] else "{}")
            localized = "true" if field["localized"] else "false"
            trailing = f", {field['minimum_payload_size']}, {names}, {field['min_form_version']}, {localized}"
        if field["max_form_version"]:
            trailing += f", {field['max_form_version']}"
        presentation = CPP_PRESENTATIONS.get(field["presentation"], field["presentation"])
        lines.append(
            "{" +
            f"Signature::from_string({cpp_string(field['record'])}), " +
            f"{cpp_string(field['path'])}, " +
            "ScalarDescriptor{" +
            f"{cpp_signature(field['subrecord'])}, " +
            f"{cpp_string(field['label'])}, " +
            f"ScalarStorage::{field['storage']}, " +
            f"ScalarPresentation::{presentation}, " +
            f"{cpp_number(field['minimum'])}, {cpp_number(field['maximum'])}, " +
            f"{field['offset']}, " +
            ("true" if field["embedded"] else "false") + f", {field['payload_size']}, " +
            render_address(field["address"]) +
            trailing +
            "}, " +
            ("true" if field["can_insert"] else "false") +
            "},"
        )
    lines.append("};")
    if arrays:
        lines.append(f"constexpr std::array<CountedArrayDescriptor, {len(arrays)}> k_counted_arrays{{{{")
        for array in arrays:
            segment = array["segment"]
            lines.append(
                "{" +
                f"Signature::from_string({cpp_string(array['record'])}), " +
                f"{cpp_string(array['path'])}, " +
                f"{cpp_string(array['label'])}, " +
                f"{cpp_signature(array['subrecord'])}, " +
                (f"{cpp_signature(array['count_subrecord'])}" if array["count_subrecord"]
                 else "Signature{}") + ", " +
                f"{array['stride']}" +
                (f", {segment['count_word']}, {segment['index']}, {segment['count']}" if segment else "") +
                "},")
        lines.append("}};")
    else:
        lines.append("constexpr std::array<CountedArrayDescriptor, 0> k_counted_arrays{};")
    lines.extend(render_subrecord_groups(groups))
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    import schema_output  # noqa: E402
    fields, arrays, groups = load_schema(args.schema)
    return schema_output.emit(fields, arrays, groups, args.output, args.check)


if __name__ == "__main__":
    raise SystemExit(main())
