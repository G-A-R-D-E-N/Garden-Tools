#!/usr/bin/env python3
"""Shared parsing and rendering of field addressing keys (repeated, stride, array_offset, when)."""

import json
from itertools import combinations

WHEN_STORAGES = {
    "u8": (1, 0, 0xFF),
    "u16": (2, 0, 0xFFFF),
    "u32": (4, 0, 0xFFFFFFFF),
    "i32": (4, -0x80000000, 0x7FFFFFFF),
}
SEGMENT_KEYS = ("count_word", "segment_index")
MAX_WHEN_VALUES = 8
# Constraint: xEdit wbModelInfoDecider (Core/wbDefinitionsCommon.pas:5646) allows at
# most eight counter words, and a larger first word must be a different format.
MAX_HEADER_COUNTERS = 8


def is_count(value, minimum):
    return type(value) is int and value >= minimum


def valid_signature(value):
    return isinstance(value, str) and len(value) == 4 and all(ord(ch) <= 0xFF for ch in value)


def printable_signature(value):
    return isinstance(value, str) and len(value) == 4 and all(0x20 <= ord(ch) <= 0x7E for ch in value)


def cpp_string(value: str) -> str:
    # Invariant: JSON escaping is only valid for the ASCII field metadata.
    return json.dumps(value, ensure_ascii=True)


def cpp_signature(value):
    if printable_signature(value):
        return f"Signature::from_string({json.dumps(value, ensure_ascii=True)})"
    packed = sum(ord(ch) << (8 * index) for index, ch in enumerate(value))
    return f"Signature::from_bytes(0x{packed:08X}u)"


def parse_when(when, path, own_subrecord):
    if not isinstance(when, dict):
        raise ValueError(f"when must be an object: {path!r}")
    unknown = set(when) - {"subrecord", "offset", "storage", "equals", "not_equals", "match_absent"}
    if unknown:
        raise ValueError(f"unknown when keys {sorted(unknown)} for {path!r}")
    subrecord = when.get("subrecord", "")
    if subrecord != "" and not printable_signature(subrecord):
        raise ValueError(f"when.subrecord must be four printable ASCII characters: {path!r}")
    if subrecord == own_subrecord:
        raise ValueError(f"when.subrecord must be omitted to read the field's own subrecord: {path!r}")
    match_absent = when.get("match_absent", False)
    if not isinstance(match_absent, bool):
        raise ValueError(f"when.match_absent must be boolean: {path!r}")
    if match_absent and subrecord == "":
        raise ValueError(f"when.match_absent requires when.subrecord: {path!r}")
    offset = when.get("offset")
    if not is_count(offset, 0):
        raise ValueError(f"when.offset must be a non-negative integer: {path!r}")
    storage = when.get("storage")
    if storage not in WHEN_STORAGES:
        raise ValueError(f"unsupported when.storage {storage!r} for {path!r}")
    _width, low, high = WHEN_STORAGES[storage]
    if ("equals" in when) == ("not_equals" in when):
        raise ValueError(f"when needs exactly one of equals or not_equals: {path!r}")
    negated = "not_equals" in when
    key = "not_equals" if negated else "equals"
    equals = when[key]
    if not isinstance(equals, list) or not equals:
        raise ValueError(f"when.{key} must be a non-empty array: {path!r}")
    if len(equals) > MAX_WHEN_VALUES:
        raise ValueError(f"when.{key} supports at most {MAX_WHEN_VALUES} values: {path!r}")
    for value in equals:
        if type(value) is not int:
            raise ValueError(f"when.{key} values must be integers: {path!r}")
        if value < low or value > high:
            raise ValueError(f"when.{key} value {value} exceeds {storage} storage for {path!r}")
    if len(set(equals)) != len(equals):
        raise ValueError(f"when.{key} contains duplicate values: {path!r}")
    return {"subrecord": subrecord, "offset": offset, "storage": storage, "equals": list(equals),
            "negated": negated, "match_absent": match_absent}


def disjoint(a, b):
    if a["negated"] and b["negated"]:
        return False
    if a["negated"]:
        a, b = b, a
    if b["negated"]:
        return set(a["equals"]) <= set(b["equals"])
    return set(a["equals"]).isdisjoint(b["equals"])


def parse_segment(row, path, stride, repeated):
    present = [key for key in SEGMENT_KEYS if key in row]
    if not present:
        return None
    if len(present) != len(SEGMENT_KEYS):
        raise ValueError(f"segment keys must be declared together: {path!r}")
    count_word = row["count_word"]
    index = row["segment_index"]
    if not is_count(count_word, 0) or count_word >= MAX_HEADER_COUNTERS:
        raise ValueError(f"count_word must index a header counter: {path!r}")
    if not is_count(index, 0) or index >= MAX_HEADER_COUNTERS:
        raise ValueError(f"segment_index must be below {MAX_HEADER_COUNTERS}: {path!r}")
    if repeated or not stride:
        raise ValueError(f"a header-counted segment needs a stride and is not repeated: {path!r}")
    if "array_offset" in row:
        raise ValueError(f"a header-counted segment starts after its own header: {path!r}")
    return {"count_word": count_word, "index": index, "count": 0}


def parse_address(row, path, subrecord, has_offset, width):
    repeated = row.get("repeated", False)
    if not isinstance(repeated, bool):
        raise ValueError(f"repeated must be boolean: {path!r}")
    stride = row.get("stride", 0)
    array_offset = row.get("array_offset", 0)
    if "stride" in row and not is_count(stride, 1):
        raise ValueError(f"stride must be a positive integer: {path!r}")
    if "array_offset" in row:
        if "stride" not in row:
            raise ValueError(f"array_offset requires stride: {path!r}")
        if not is_count(array_offset, 0):
            raise ValueError(f"array_offset must be a non-negative integer: {path!r}")
    # Constraint: repeated with stride means every matching subrecord contributes its own strided elements, which is how a struct array that repeats inside one record (RACE TTEC) is addressed.
    if stride:
        if not has_offset:
            raise ValueError(f"stride requires an offset: {path!r}")
        if row.get("offset", 0) + width > stride:
            raise ValueError(f"field does not fit inside one stride element: {path!r}")
    segment = parse_segment(row, path, stride, repeated)
    when = parse_when(row["when"], path, subrecord) if "when" in row else None
    if when is not None and not when["subrecord"]:
        end = when["offset"] + WHEN_STORAGES[when["storage"]][0]
        if stride and end > stride:
            raise ValueError(f"when discriminator does not fit inside one stride element: {path!r}")
        if not stride and row.get("payload_size") and end > row["payload_size"]:
            raise ValueError(f"when discriminator does not fit inside payload_size: {path!r}")
    return {"repeated": repeated, "stride": stride, "array_offset": array_offset, "when": when,
            "segment": segment}


def address_key(address):
    return json.dumps(address, sort_keys=True)


def form_version_windows_disjoint(first, second):
    first_low = first.get("min_form_version", 0)
    first_high = first.get("max_form_version", 0)
    second_low = second.get("min_form_version", 0)
    second_high = second.get("max_form_version", 0)
    return bool((first_high and first_high <= second_low) or (second_high and second_high <= first_low))


def field_span(field):
    start = field["offset"]
    end = start + field["width"]
    if end <= start:
        end = max(field.get("minimum_payload_size", 0), field.get("payload_size", 0)) or float("inf")
    return start, end


def reject_overlaps(entries, kind_label):
    by_payload = {}
    for field in entries:
        by_payload.setdefault((field["record"], field["subrecord"]), []).append(field)
    for (record_signature, subrecord), group in by_payload.items():
        for first, second in combinations(group, 2):
            a, b = first["address"], second["address"]
            different_layout = any(a[key] != b[key] for key in ("repeated", "stride", "array_offset"))
            # Constraint: a subrecord_group member row addresses its own run instance, so a row of the same signature outside the group cannot share its bytes.
            if different_layout and bool(first.get("group_member")) != bool(second.get("group_member")):
                continue
            if first["embedded"] and second["embedded"] and form_version_windows_disjoint(first, second):
                continue
            aw, bw = a["when"], b["when"]
            if aw is None or bw is None:
                exclusivity = "one or both fields are unguarded"
            elif any(aw[key] != bw[key] for key in ("subrecord", "offset", "storage")):
                exclusivity = "different when discriminators are not mutually exclusive"
            elif aw["match_absent"] and bw["match_absent"]:
                exclusivity = "both when guards match an absent discriminator"
            elif disjoint(aw, bw):
                if aw["subrecord"] or not different_layout:
                    continue
                exclusivity = "own-element discriminators use different layouts"
            else:
                exclusivity = "when value sets intersect"

            if different_layout:
                conflict = "conflicting repeated/stride/array_offset layouts"
            elif not first["embedded"] or not second["embedded"]:
                conflict = "whole-payload field covers the subrecord"
            else:
                start_a, end_a = field_span(first)
                start_b, end_b = field_span(second)
                if start_a >= end_b or start_b >= end_a:
                    continue
                conflict = f"byte ranges [{start_a}, {end_a}) and [{start_b}, {end_b}) intersect"
            raise ValueError(
                f"overlapping {kind_label} fields in {record_signature}.{subrecord}: "
                f"{first['path']!r} and {second['path']!r}: {conflict}; {exclusivity}")


def render_address(address):
    when = address["when"]
    segment = address.get("segment")
    trailing = ""
    if segment is not None:
        trailing = f", SegmentAddress{{{segment['count_word']}, {segment['index']}, {segment['count']}}}"
    if not address["repeated"] and not address["stride"] and when is None and not trailing:
        return "FieldAddress{}"
    if when is None:
        rendered = "FieldDiscriminator{}"
    else:
        values = ", ".join(str(value) for value in when["equals"])
        subrecord = (f"Signature::from_string({json.dumps(when['subrecord'])})"
                     if when["subrecord"] else "Signature{}")
        rendered = (f"FieldDiscriminator{{{subrecord}, {when['offset']}, "
                    f"DiscriminatorStorage::{when['storage']}, {len(when['equals'])}, {{{{{values}}}}}"
                    f"{', true' if when['match_absent'] else ', false'}"
                    f"{', true' if when['negated'] else ', false'}}}")
    return (f"FieldAddress{{{'true' if address['repeated'] else 'false'}, {address['stride']}, "
            f"{address['array_offset']}, {rendered}{trailing}}}")


HEX_DIGITS = "0123456789abcdefABCDEF"


def reject_bytes_row(field, path, embedded, presentation):
    if embedded:
        raise ValueError(f"bytes storage must cover the whole payload: {path!r}")
    if presentation != "identity":
        raise ValueError(f"bytes storage requires identity presentation: {path!r}")
    if "names" in field:
        raise ValueError(f"bytes storage does not accept names: {path!r}")
    if "default" not in field:
        return
    default = field["default"]
    if not isinstance(default, str) or any(ch not in HEX_DIGITS for ch in default):
        raise ValueError(f"bytes row default must be hexadecimal: {path!r}")
    if len(default) % 2 != 0:
        raise ValueError(f"bytes row default must be an even-length hex string: {path!r}")
    raise ValueError(f"bytes row default is not applied by any insertion policy: {path!r}")
