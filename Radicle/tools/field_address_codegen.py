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
INLINE_WHEN_VALUES = 8
# Constraint: overflow values are emitted once per generated file, so a value set has no cap.
# Constraint: each generated file passes its own value-set name, because the arrays are inline and a name shared by two files would merge different contents.
DEFAULT_VALUE_SET = "k_generated_discriminator_values"
_EXTRA_VALUE_SETS = {}


def pool_discriminator_values(values, name=DEFAULT_VALUE_SET):
    pool = _EXTRA_VALUE_SETS.setdefault(name, [])
    pool.append([int(value) for value in values])
    return len(pool) - 1


def discriminator_value_lines(name=DEFAULT_VALUE_SET):
    lines = [f"inline constexpr std::int64_t {name}_{index}[] = {{"
             + ", ".join(str(value) for value in values) + "};"
             for index, values in enumerate(_EXTRA_VALUE_SETS.pop(name, []))]
    return lines
# Constraint: xEdit wbModelInfoDecider (Core/wbDefinitionsCommon.pas:5646) allows at
# most eight counter words, and a larger first word must be a different format.
MAX_HEADER_COUNTERS = 8
RUN_KEYS = {"start", "before", "after", "count_offset", "count_width", "count_inline"}
RUN_SECTION_KEYS = {"stride", "count_offset", "count_width", "count_inline"}
RUN_COUNT_KEYS = ("count_offset", "count_width", "count_inline")
# Constraint: this count_offset marks a section whose four-byte count word sits at the running cursor; run_span reads the same value.
RUN_INLINE_COUNT = 0xFFFF
RUN_COUNT_WIDTHS = (1, 2, 4)
# Constraint: the generated RunAddress stores offsets and strides in uint16_t and holds at most this many sections.
MAX_RUN_SECTIONS = 6
MAX_RUN_VALUE = 0xFFFF


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
    unknown = set(when) - {"subrecord", "offset", "storage", "equals", "not_equals", "match_absent",
                          "bits"}
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
    if "bits" in when:
        if "equals" in when or "not_equals" in when:
            raise ValueError(f"when.bits cannot combine with equals or not_equals: {path!r}")
        bits = when["bits"]
        if type(bits) is not int or bits <= 0 or bits > high:
            raise ValueError(f"when.bits must be a positive mask inside {storage} for {path!r}")
        return {"subrecord": subrecord, "offset": offset, "storage": storage, "equals": [],
                "negated": False, "match_absent": match_absent, "bits": bits, "extra": []}
    if ("equals" in when) == ("not_equals" in when):
        raise ValueError(f"when needs exactly one of equals, not_equals or bits: {path!r}")
    negated = "not_equals" in when
    key = "not_equals" if negated else "equals"
    equals = when[key]
    if not isinstance(equals, list) or not equals:
        raise ValueError(f"when.{key} must be a non-empty array: {path!r}")
    for value in equals:
        if type(value) is not int:
            raise ValueError(f"when.{key} values must be integers: {path!r}")
        if value < low or value > high:
            raise ValueError(f"when.{key} value {value} exceeds {storage} storage for {path!r}")
    if len(set(equals)) != len(equals):
        raise ValueError(f"when.{key} contains duplicate values: {path!r}")
    return {"subrecord": subrecord, "offset": offset, "storage": storage, "equals": list(equals),
            "negated": negated, "match_absent": match_absent, "bits": 0,
            "extra": list(equals[INLINE_WHEN_VALUES:])}


def disjoint(a, b):
    if a.get("bits") or b.get("bits"):
        return False
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


def parse_run_count(section, stride, path, start):
    present = [key for key in ("count_offset", "count_width") if key in section]
    if len(present) == 1:
        raise ValueError(f"run count_offset and count_width must be declared together: {path!r}")
    if not present:
        return {"count_offset": 0, "count_width": 0, "stride": stride}
    offset, width = section["count_offset"], section["count_width"]
    if not is_count(offset, 0) or offset > MAX_RUN_VALUE:
        raise ValueError(f"run count_offset must be a 16-bit non-negative integer: {path!r}")
    if type(width) is not int or width not in RUN_COUNT_WIDTHS:
        raise ValueError(f"run count_width must be 1, 2 or 4: {path!r}")
    if offset + width > start:
        raise ValueError(f"run counts must lie in the fixed header before start: {path!r}")
    return {"count_offset": offset, "count_width": width, "stride": stride}


def parse_run_section(section, path, start):
    if not isinstance(section, dict) or set(section) - RUN_SECTION_KEYS:
        raise ValueError(f"run section keys must be a subset of {sorted(RUN_SECTION_KEYS)}: {path!r}")
    stride = section.get("stride")
    if not is_count(stride, 1) or stride > MAX_RUN_VALUE:
        raise ValueError(f"run section stride must be a positive 16-bit integer: {path!r}")
    if "count_inline" in section:
        if section["count_inline"] is not True or "count_offset" in section or "count_width" in section:
            raise ValueError(f"run count_inline must be true and replaces count_offset and count_width: {path!r}")
        return {"count_offset": RUN_INLINE_COUNT, "count_width": 4, "stride": stride}
    return parse_run_count(section, stride, path, start)


def parse_run(row, path, stride, segment):
    if "run" not in row:
        return None
    run = row["run"]
    if not isinstance(run, dict) or set(run) - RUN_KEYS:
        raise ValueError(f"run keys must be a subset of {sorted(RUN_KEYS)}: {path!r}")
    if segment is not None:
        raise ValueError(f"a run row is not a header-counted segment: {path!r}")
    if "when" in row or "array_offset" in row:
        raise ValueError(f"a run row does not accept when or array_offset: {path!r}")
    if not is_count(stride, 1) or stride > MAX_RUN_VALUE:
        raise ValueError(f"a run row needs a 16-bit stride: {path!r}")
    start = run.get("start")
    if not is_count(start, 0) or start > MAX_RUN_VALUE:
        raise ValueError(f"run start must be a 16-bit non-negative integer: {path!r}")
    before = run.get("before", [])
    after = run.get("after", [])
    if not isinstance(before, list) or not isinstance(after, list) or len(before) + len(after) + 1 > MAX_RUN_SECTIONS:
        raise ValueError(f"run before and after must be arrays of at most {MAX_RUN_SECTIONS - 1} sections together: {path!r}")
    sections = [parse_run_section(section, path, start) for section in before]
    sections.append(parse_run_section({"stride": stride, **{key: run[key] for key in RUN_COUNT_KEYS if key in run}}, path, start))
    sections.extend(parse_run_section(section, path, start) for section in after)
    return {"start": start, "count": len(sections), "index": len(before), "sections": sections}


def reject_run_disagreement(entries):
    by_payload = {}
    for field in entries:
        run = field["address"].get("run")
        if run is not None:
            by_payload.setdefault((field["record"], field["subrecord"]), []).append(field)
    for (record_signature, subrecord), group in by_payload.items():
        for first, second in combinations(group, 2):
            a, b = first["address"]["run"], second["address"]["run"]
            shared = min(a["count"], b["count"])
            if a["start"] != b["start"] or a["sections"][:shared] != b["sections"][:shared]:
                raise ValueError(
                    f"run layouts of {record_signature}.{subrecord} disagree: "
                    f"{first['path']!r} and {second['path']!r}")


def parse_address(row, path, subrecord, has_offset, width, allow_run=False):
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
    if "run" in row and not allow_run:
        raise ValueError(f"run is only valid on counted_array member rows: {path!r}")
    run = parse_run(row, path, stride, segment)
    when = parse_when(row["when"], path, subrecord) if "when" in row else None
    if when is not None and not when["subrecord"]:
        end = when["offset"] + WHEN_STORAGES[when["storage"]][0]
        if stride and end > stride:
            raise ValueError(f"when discriminator does not fit inside one stride element: {path!r}")
        if not stride and row.get("payload_size") and end > row["payload_size"]:
            raise ValueError(f"when discriminator does not fit inside payload_size: {path!r}")
    occurrence = row.get("subrecord_occurrence")
    if occurrence is not None and not is_count(occurrence, 0):
        raise ValueError(f"subrecord_occurrence must be a non-negative integer: {path!r}")
    return {"repeated": repeated, "stride": stride, "array_offset": array_offset, "when": when,
            "segment": segment, "run": run, "subrecord_occurrence": occurrence or 0,
            "has_subrecord_occurrence": occurrence is not None}


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


def render_address(address, value_set=DEFAULT_VALUE_SET):
    when = address["when"]
    segment = address.get("segment")
    trailing = ""
    if segment is not None:
        trailing = f", SegmentAddress{{{segment['count_word']}, {segment['index']}, {segment['count']}}}"
    run = address.get("run")
    occurrence = address.get("has_subrecord_occurrence")
    if (run is not None or occurrence) and segment is None:
        trailing = ", SegmentAddress{}"
    if run is not None:
        sections = ", ".join("{" + f"{item['count_offset']}, {item['count_width']}, {item['stride']}" + "}"
                             for item in run["sections"])
        trailing += (", RunAddress{" + f"{run['start']}, {run['count']}, {run['index']}, "
                     + "{{" + sections + "}}}")
    elif occurrence:
        trailing += ", RunAddress{}"
    if occurrence:
        trailing += f", {address['subrecord_occurrence']}, true"
    if not address["repeated"] and not address["stride"] and when is None and not trailing:
        return "FieldAddress{}"
    if when is None:
        rendered = "FieldDiscriminator{}"
    else:
        inline = when["equals"][:INLINE_WHEN_VALUES]
        values = ", ".join(str(value) for value in inline)
        subrecord = (f"Signature::from_string({json.dumps(when['subrecord'])})"
                     if when["subrecord"] else "Signature{}")
        extra = when.get("extra") or []
        extra_name = (f"{value_set}_{pool_discriminator_values(extra, value_set)}"
                      if extra else "nullptr")
        rendered = (f"FieldDiscriminator{{{subrecord}, {when['offset']}, "
                    f"DiscriminatorStorage::{when['storage']}, {len(inline)}, {{{{{values}}}}}"
                    f"{', true' if when['match_absent'] else ', false'}"
                    f"{', true' if when['negated'] else ', false'}, {when.get('bits') or 0}, "
                    f"{extra_name}, {len(extra)}}}")
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
