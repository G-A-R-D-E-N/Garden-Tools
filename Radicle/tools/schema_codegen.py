#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_address_codegen import is_count, parse_address, reject_overlaps, render_address  # noqa: E402

STORAGES = {"u8", "u16", "u32", "i32", "f32", "i8", "i16"}
KINDS = {"scalar", "string"}
STRING_FORBIDDEN = {"storage", "presentation", "minimum", "maximum", "offset", "payload_size", "minimum_payload_size", "stride", "array_offset", "names"}
PRESENTATIONS = {"identity", "percent_0_100", "enum", "flags"}
NAME_KEYS = {"enum": "value", "flags": "bit"}
CPP_PRESENTATIONS = {"enum": "enumeration"}
# Missing-field insertion is intentionally not generated yet. Safe insertion
# requires record-level subrecord ordering metadata, not only a scalar field row.
INSERTIONS = {"existing_only"}
COUNTED_ARRAY_KEYS = {"kind", "path", "label", "subrecord", "count_subrecord", "stride", "insertion", "members"}
MEMBER_KEYS = {"path", "label", "offset", "storage", "presentation", "minimum", "maximum"}
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
    return len(value) == 4 and all(0x20 <= ord(ch) <= 0x7E for ch in value)


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
    for signature in (subrecord, count_subrecord):
        if not isinstance(signature, str) or not valid_signature(signature):
            raise ValueError(f"counted_array signatures must be four printable ASCII characters: {signature!r}: {path!r}")
    if count_subrecord == subrecord:
        raise ValueError(f"counted_array count_subrecord must differ from its subrecord: {path!r}")
    stride = field.get("stride")
    if not is_count(stride, 1):
        raise ValueError(f"counted_array stride must be a positive integer: {path!r}")
    if field.get("insertion") not in INSERTIONS:
        raise ValueError(f"unsupported insertion policy until record ordering exists: {field.get('insertion')!r}")
    members = field.get("members")
    if not isinstance(members, list) or not members:
        raise ValueError(f"counted_array members must be a non-empty array: {path!r}")
    rows = []
    for member in members:
        if not isinstance(member, dict) or set(member) - MEMBER_KEYS:
            raise ValueError(f"counted_array member keys must be a subset of {sorted(MEMBER_KEYS)}: {path!r}")
        rows.append(dict(member, kind="scalar", subrecord=subrecord, stride=stride, insertion="existing_only"))
    array = {"record": record_signature, "path": path, "label": label, "subrecord": subrecord,
             "count_subrecord": count_subrecord, "stride": stride}
    return array, rows


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
                storage = "zstring"
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
                if not embedded:
                    raise ValueError(f"payload_size requires an embedded field: {path!r}")
                if type(payload_size) is not int or payload_size <= 0:
                    raise ValueError(f"payload_size must be a positive integer: {path!r}")
                if payload_size < offset + WIDTHS[storage]:
                    raise ValueError(f"payload_size is smaller than the field end: {path!r}")
                if address["stride"] and (payload_size - address["array_offset"]) % address["stride"] != 0:
                    raise ValueError(f"payload_size does not hold whole stride elements: {path!r}")

            minimum_payload_size = field.get("minimum_payload_size", 0)
            if "minimum_payload_size" in field:
                if not embedded:
                    raise ValueError(f"minimum_payload_size requires an embedded field: {path!r}")
                if type(minimum_payload_size) is not int or minimum_payload_size <= 0:
                    raise ValueError(f"minimum_payload_size must be a positive integer: {path!r}")
                if minimum_payload_size < offset + WIDTHS[storage]:
                    raise ValueError(f"minimum_payload_size is smaller than the field end: {path!r}")
                if payload_size and minimum_payload_size > payload_size:
                    raise ValueError(f"minimum_payload_size exceeds payload_size: {path!r}")

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
                "address": address,
                "can_insert": False,
            })

    for array in arrays:
        twins = [other for other in arrays
                 if (other["record"], other["subrecord"]) == (array["record"], array["subrecord"])]
        if len(twins) > 1:
            raise ValueError(f"counted_array subrecord {array['subrecord']} is declared twice in {array['record']}: {array['path']!r}")
        counts = [row for row in fields
                  if row["record"] == array["record"] and row["subrecord"] == array["count_subrecord"]]
        if len(counts) != 1 or not whole_payload_u32(counts[0]):
            raise ValueError(f"counted_array {array['path']!r} needs exactly one whole-payload u32 row for {array['count_subrecord']}")

    reject_overlaps([dict(field, width=WIDTHS.get(field["storage"], 0)) for field in fields], "scalar")

    fields.sort(key=lambda item: (item["record"], item["path"]))
    arrays.sort(key=lambda item: (item["record"], item["path"]))
    return fields, arrays


def names_key(field):
    return tuple((entry["value"], entry["name"]) for entry in field["names"])


def render(fields, arrays=()) -> str:
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
        elif field["minimum_payload_size"]:
            trailing = f", {field['minimum_payload_size']}"
        presentation = CPP_PRESENTATIONS.get(field["presentation"], field["presentation"])
        lines.append(
            "{" +
            f"Signature::from_string({cpp_string(field['record'])}), " +
            f"{cpp_string(field['path'])}, " +
            "ScalarDescriptor{" +
            f"Signature::from_string({cpp_string(field['subrecord'])}), " +
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
            lines.append(
                "{" +
                f"Signature::from_string({cpp_string(array['record'])}), " +
                f"{cpp_string(array['path'])}, " +
                f"{cpp_string(array['label'])}, " +
                f"Signature::from_string({cpp_string(array['subrecord'])}), " +
                f"Signature::from_string({cpp_string(array['count_subrecord'])}), " +
                f"{array['stride']}" +
                "},")
        lines.append("}};")
    else:
        lines.append("constexpr std::array<CountedArrayDescriptor, 0> k_counted_arrays{};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    fields, arrays = load_schema(args.schema)
    expected = render(fields, arrays)
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != expected:
            print(f"generated schema is stale: {args.output}")
            return 1
        return 0

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(expected, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
