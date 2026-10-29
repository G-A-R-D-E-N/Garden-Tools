#!/usr/bin/env python3

import argparse
import json
from pathlib import Path

PLACEMENTS = {"top_level", "quest_children", "topic_children", "cell_children", "cell_block", "cell_reference_children"}


def valid_signature(value: str) -> bool:
    return len(value) == 4 and all(0x20 <= ord(ch) <= 0x7E for ch in value)


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def parse_default(value, record: str, signature: str, payload_sizes, minimum_payload_sizes):
    if not isinstance(value, str) or len(value) % 2 != 0 or len(value) > 0xFFFF * 2:
        raise ValueError(f"authoring default must be an even-length hex string for {record}.{signature}")
    if any(ch not in "0123456789abcdefABCDEF" for ch in value):
        raise ValueError(f"authoring default must be hexadecimal for {record}.{signature}")
    data = bytes.fromhex(value)
    for size in payload_sizes.get(signature, ()):
        if size != len(data):
            raise ValueError(
                f"authoring default for {record}.{signature} is {len(data)} bytes but a scalar field expects {size}")
    minimum = minimum_payload_sizes.get(signature, 0)
    if len(data) < minimum:
        raise ValueError(
            f"authoring default for {record}.{signature} is {len(data)} bytes but a scalar field needs at least {minimum}")
    return data


def load_constraints(schema_path: Path):
    document = json.loads(schema_path.read_text(encoding="utf-8"))
    if document.get("version") != 1:
        raise ValueError("unsupported Fallout 4 field schema version")

    records = document.get("records")
    if not isinstance(records, list):
        raise ValueError("schema records must be an array")

    constraints = []
    seen_records = set()
    for record in records:
        if not isinstance(record, dict):
            raise ValueError("schema record entries must be objects")
        signature = record.get("signature", "")
        if not isinstance(signature, str) or not valid_signature(signature):
            raise ValueError(
                f"record signature must be four printable ASCII characters: {signature!r}")

        authoring = record.get("authoring")
        if authoring is None:
            continue
        if signature in seen_records:
            raise ValueError(f"duplicate authoring schema for record: {signature}")
        seen_records.add(signature)
        if not isinstance(authoring, dict):
            raise ValueError(f"authoring metadata must be an object: {signature}")

        placement = authoring.get("placement")
        if placement not in PLACEMENTS:
            raise ValueError(f"unsupported authoring placement for {signature}: {placement!r}")

        subrecords = authoring.get("subrecords")
        if not isinstance(subrecords, list):
            raise ValueError(f"authoring subrecords must be an array: {signature}")

        payload_sizes = {}
        minimum_payload_sizes = {}
        for field in record.get("fields", []):
            if isinstance(field, dict) and "payload_size" in field and "subrecord" in field:
                payload_sizes.setdefault(field["subrecord"], set()).add(field["payload_size"])
            if isinstance(field, dict) and "minimum_payload_size" in field and "subrecord" in field:
                minimum_payload_sizes[field["subrecord"]] = max(
                    minimum_payload_sizes.get(field["subrecord"], 0), field["minimum_payload_size"])

        parsed_subrecords = []
        seen_subrecords = set()
        for subrecord in subrecords:
            if not isinstance(subrecord, dict):
                raise ValueError(f"authoring subrecord entries must be objects: {signature}")
            sub_signature = subrecord.get("signature", "")
            if not isinstance(sub_signature, str) or not valid_signature(sub_signature):
                raise ValueError(
                    f"subrecord signature must be four printable ASCII characters: {sub_signature!r}")
            if sub_signature in seen_subrecords:
                raise ValueError(
                    f"duplicate authoring subrecord for {signature}: {sub_signature}")
            seen_subrecords.add(sub_signature)

            minimum = subrecord.get("minimum")
            maximum = subrecord.get("maximum")
            if isinstance(minimum, bool) or not isinstance(minimum, int) or minimum < 0:
                raise ValueError(
                    f"authoring minimum must be a non-negative integer for {signature}.{sub_signature}")
            if isinstance(maximum, bool) or not isinstance(maximum, int) or maximum < minimum:
                raise ValueError(
                    f"authoring maximum must be an integer >= minimum for {signature}.{sub_signature}")
            if maximum > 0xFFFF:
                raise ValueError(
                    f"authoring maximum exceeds u16 storage for {signature}.{sub_signature}")

            default = None
            if "default" in subrecord:
                default = parse_default(subrecord["default"], signature, sub_signature, payload_sizes,
                                        minimum_payload_sizes)

            uncounted = subrecord.get("uncounted", False)
            if not isinstance(uncounted, bool):
                raise ValueError(f"authoring uncounted must be a boolean for {signature}.{sub_signature}")
            if uncounted and maximum <= 1:
                raise ValueError(f"authoring uncounted only applies to repeated entries for {signature}.{sub_signature}")

            coupled = subrecord.get("coupled", False)
            if not isinstance(coupled, bool):
                raise ValueError(f"authoring coupled must be a boolean for {signature}.{sub_signature}")

            paired_with = subrecord.get("paired_with")
            if paired_with is not None and (not isinstance(paired_with, str)
                                            or not valid_signature(paired_with)):
                raise ValueError(
                    f"authoring paired_with must be a subrecord signature for {signature}.{sub_signature}")

            parsed_subrecords.append({
                "signature": sub_signature,
                "minimum": minimum,
                "maximum": maximum,
                "default": default,
                "uncounted": uncounted,
                "coupled": coupled,
                "paired_with": paired_with,
            })

        known_subrecords = {item["signature"] for item in parsed_subrecords}
        for item in parsed_subrecords:
            if item["paired_with"] is None:
                continue
            if item["paired_with"] == item["signature"] or item["paired_with"] not in known_subrecords:
                raise ValueError(
                    f"authoring paired_with must name another subrecord of {signature}: "
                    f"{item['signature']} paired with {item['paired_with']}")

        constraints.append({
            "signature": signature,
            "placement": placement,
            "subrecords": parsed_subrecords,
        })

    constraints.sort(key=lambda item: item["signature"])
    return constraints


GENERATED_HEADER = (
    "// Generated by Radicle/tools/record_schema_codegen.py from schema/fallout4/fields.json.",
    "// Do not hand-edit; update the JSON source and regenerate.",
)

# Constraint: shards are included in order inside one array so every index stays an offset.
SHARD_ENTRY_LIMIT = 600
SHARD_LINE_LIMIT = 750


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
        raise ValueError(f"generated authoring schema shard exceeds {SHARD_LINE_LIMIT} lines: {name}")
    return "\n".join(text) + "\n"


def shard_files(output: Path):
    return sorted(output.parent.glob(f"{output.stem}_*_shard[0-9][0-9].inc"))


def render(constraints, stem: str = "generated_record_constraints"):
    shards = {}
    lines = list(GENERATED_HEADER)

    default_names = {}
    for record in constraints:
        for subrecord in record["subrecords"]:
            if subrecord["default"] is None:
                continue
            name = f"k_generated_authoring_default_{len(default_names)}"
            default_names[(record["signature"], subrecord["signature"])] = name
            body = ", ".join(f"0x{byte:02X}" for byte in subrecord["default"]) or "0x00"
            bound = "" if subrecord["default"] else "1"
            lines.append(f"inline constexpr std::uint8_t {name}[{bound}] = {{{body}}};")
    def emit_array(declaration: str, name: str, entries) -> None:
        lines.append(f"inline constexpr {declaration} {name}[] = {{")
        if len(entries) > SHARD_ENTRY_LIMIT:
            chunks = chunk_entries(entries)
            for index, chunk in enumerate(chunks):
                shard = f"{stem}_{name.removeprefix('k_generated_')}_shard{index:02d}.inc"
                shards[shard] = render_shard(name, index, len(chunks), chunk)
                lines.append(f'#include "{shard}"')
        else:
            lines.extend(text for _record, text in entries)
        lines.append("};")
        lines.append("")

    first = 0
    rule_entries = []
    records = []
    for record in constraints:
        for subrecord in record["subrecords"]:
            name = default_names.get((record["signature"], subrecord["signature"]))
            tail = "nullptr, 0" if name is None else f"{name}, {len(subrecord['default'])}"
            rule_entries.append((record["signature"],
                "  {" +
                f"Signature::from_string({cpp_string(subrecord['signature'])}), " +
                f"{subrecord['minimum']}, {subrecord['maximum']}, {tail}, " +
                ("true" if subrecord["uncounted"] else "false") + ", " +
                ("true" if subrecord["coupled"] else "false") +
                "},"))
        records.append((record, first, len(record["subrecords"])))
        first += len(record["subrecords"])
    emit_array("SubrecordAuthoringConstraint", "k_generated_subrecord_authoring_constraints",
               rule_entries)

    record_entries = []
    for record, start, count in records:
        record_entries.append((record["signature"],
            "  {" +
            f"Signature::from_string({cpp_string(record['signature'])}), " +
            f"RecordPlacement::{record['placement']}, {start}, {count}" +
            "},"))
    emit_array("RecordAuthoringConstraint", "k_generated_record_authoring_constraints", record_entries)
    pairs = [(record, subrecord)
             for record in constraints
             for subrecord in record["subrecords"]
             if subrecord["paired_with"] is not None]
    if pairs:
        lines.append("inline constexpr SubrecordPairConstraint k_generated_subrecord_pairs[] = {")
        for record, subrecord in pairs:
            lines.append(
                "  {" +
                f"Signature::from_string({cpp_string(record['signature'])}), " +
                f"Signature::from_string({cpp_string(subrecord['signature'])}), " +
                f"Signature::from_string({cpp_string(subrecord['paired_with'])})" +
                "},"
            )
        lines.append("};")
    else:
        lines.append("inline constexpr std::array<SubrecordPairConstraint, 0> "
                     "k_generated_subrecord_pairs{};")
    return "\n".join(lines) + "\n", shards


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    parent, shards = render(load_constraints(args.schema), args.output.stem)
    expected = {args.output: parent}
    for name, text in shards.items():
        expected[args.output.with_name(name)] = text
    orphans = [path for path in shard_files(args.output) if path not in expected]

    if args.check:
        ok = True
        for path, text in expected.items():
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                print(f"generated record authoring schema is stale: {path}")
                ok = False
        for path in orphans:
            print(f"generated record authoring schema has an orphan shard: {path}")
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
