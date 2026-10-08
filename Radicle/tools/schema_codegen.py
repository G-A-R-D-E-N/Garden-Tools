#!/usr/bin/env python3

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from field_address_codegen import parse_address, reject_overlaps, render_address  # noqa: E402

STORAGES = {"u8", "u16", "u32", "i32", "f32", "i8", "i16"}
KINDS = {"scalar", "string"}
STRING_FORBIDDEN = {"storage", "presentation", "minimum", "maximum", "offset", "payload_size", "minimum_payload_size", "stride", "array_offset"}
PRESENTATIONS = {"identity", "percent_0_100"}
# Missing-field insertion is intentionally not generated yet. Safe insertion
# requires record-level subrecord ordering metadata, not only a scalar field row.
INSERTIONS = {"existing_only"}
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


def load_fields(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 field schema version")

    records = document.get("records")
    if not isinstance(records, list):
        raise ValueError("schema records must be an array")

    fields = []
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

        for field in record_fields:
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

            fields.append({
                "record": record_signature,
                "path": path,
                "label": label,
                "subrecord": subrecord,
                "storage": storage,
                "presentation": presentation,
                "minimum": minimum,
                "maximum": maximum,
                "offset": offset,
                "embedded": embedded,
                "payload_size": payload_size,
                "minimum_payload_size": minimum_payload_size,
                "address": address,
                "can_insert": False,
            })

    reject_overlaps([dict(field, width=WIDTHS.get(field["storage"], 0)) for field in fields], "scalar")

    fields.sort(key=lambda item: (item["record"], item["path"]))
    return fields


def render(fields) -> str:
    lines = [
        "// Generated by Radicle/tools/schema_codegen.py from schema/fallout4/fields.json.",
        "// Do not hand-edit; update the JSON source and regenerate.",
    ]
    for field in fields:
        lines.append(
            "{" +
            f"Signature::from_string({cpp_string(field['record'])}), " +
            f"{cpp_string(field['path'])}, " +
            "ScalarDescriptor{" +
            f"Signature::from_string({cpp_string(field['subrecord'])}), " +
            f"{cpp_string(field['label'])}, " +
            f"ScalarStorage::{field['storage']}, " +
            f"ScalarPresentation::{field['presentation']}, " +
            f"{cpp_number(field['minimum'])}, {cpp_number(field['maximum'])}, " +
            f"{field['offset']}, " +
            ("true" if field["embedded"] else "false") + f", {field['payload_size']}, " +
            render_address(field["address"]) +
            (f", {field['minimum_payload_size']}" if field["minimum_payload_size"] else "") +
            "}, " +
            ("true" if field["can_insert"] else "false") +
            "},"
        )
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    expected = render(load_fields(args.schema))
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
