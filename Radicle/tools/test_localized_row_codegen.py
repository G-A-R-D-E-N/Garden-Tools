#!/usr/bin/env python3
"""Validation of the localized flag on string rows in schema_codegen.py and the record authoring tables in record_schema_codegen.py."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import record_schema_codegen  # noqa: E402
import schema_codegen  # noqa: E402

BS = chr(92)


def string(**overrides):
    row = {
        "path": "TEST" + BS + "Text",
        "label": "Text",
        "kind": "string",
        "subrecord": "DATA",
        "insertion": "existing_only",
    }
    row.update(overrides)
    return row


def scalar(**overrides):
    row = {
        "path": "TEST" + BS + "Number",
        "label": "Number",
        "kind": "scalar",
        "subrecord": "DATA",
        "storage": "u32",
        "presentation": "identity",
        "minimum": 0,
        "maximum": 100,
        "offset": 0,
        "insertion": "existing_only",
    }
    row.update(overrides)
    return row


class LocalizedRowTests(unittest.TestCase):
    def load(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fields.json"
            document = {"version": 1, "records": [{"signature": "TEST", "fields": list(fields)}]}
            path.write_text(json.dumps(document), encoding="utf-8")
            return schema_codegen.load_fields(path)

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load(*fields)

    def test_string_rows_accept_a_boolean_flag(self):
        self.assertTrue(self.load(string(localized=True))[0]["localized"])
        self.assertFalse(self.load(string(localized=False))[0]["localized"])
        self.assertFalse(self.load(string())[0]["localized"])

    def test_localized_string_row_renders_the_flag(self):
        rendered = schema_codegen.render(self.load(string(localized=True)))
        self.assertIn("{}, 0, true}", rendered)
        self.assertIn("FieldAddress{}}, false}", schema_codegen.render(self.load(string(localized=False))))

    def test_flag_must_be_a_boolean(self):
        for value in ("yes", 1, 0, None):
            with self.subTest(value=value):
                self.rejects("localized must be a boolean", string(localized=value))

    def test_flag_is_only_for_string_rows(self):
        self.rejects("string rows only", scalar(localized=True))
        self.assertFalse(self.load(scalar(localized=False))[0]["localized"])

    def test_slotted_string_rows_carry_their_offset(self):
        field = self.load(string(offset=260, minimum_payload_size=520))[0]
        self.assertEqual((field["storage"], field["embedded"], field["offset"],
                          field["minimum_payload_size"]), ("zstring", True, 260, 520))
        self.rejects("minimum_payload_size requires an embedded field",
                     string(offset=None, minimum_payload_size=520))
        self.rejects("minimum_payload_size is smaller than the field end",
                     string(offset=520, minimum_payload_size=260))

    def test_a_payload_size_makes_a_whole_payload_fixed_width_name(self):
        field = self.load(string(payload_size=4, repeated=True))[0]
        self.assertEqual((field["storage"], field["payload_size"], field["embedded"]),
                         ("fixed_string", 4, False))
        rendered = schema_codegen.render([field])
        self.assertIn("ScalarStorage::fixed_string, ScalarPresentation::identity, 0.0, 0.0, 0, false, 4, "
                      "FieldAddress{true, 0, 0, FieldDiscriminator{}}", rendered)
        self.assertEqual(self.load(string())[0]["storage"], "zstring")
        for value in (0, -4, True, "4"):
            with self.subTest(value=value):
                self.rejects("payload_size must be a positive integer", string(payload_size=value))
        self.rejects("must cover the whole payload", string(payload_size=4, offset=0))

    def test_slots_must_not_share_a_subrecord_range(self):
        slots = [string(path="TEST" + BS + "First", label="First", offset=0, minimum_payload_size=260),
                 string(path="TEST" + BS + "Second", label="Second", offset=260, minimum_payload_size=520)]
        self.assertEqual(len(self.load(*slots)), 2)
        self.rejects("byte ranges", slots[0],
                     string(path="TEST" + BS + "Second", label="Second", offset=0, minimum_payload_size=260))
        self.rejects("byte ranges", slots[0], scalar(path="TEST" + BS + "Number", offset=4))

    def test_a_slot_without_a_payload_floor_spans_to_the_payload_end(self):
        slot = string(subrecord="DATA", offset=0)
        self.assertEqual(len(self.load(slot)), 1)
        self.rejects("byte ranges", slot, scalar(path="TEST" + BS + "Number", offset=4))
        self.rejects("byte ranges", slot, string(subrecord="DATA", path="TEST" + BS + "Other",
                                                 label="Other", offset=4))


class RecordPairCodegenTests(unittest.TestCase):
    def render(self, *subrecords):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fields.json"
            document = {
                "version": 1,
                "records": [{
                    "signature": "TEST",
                    "authoring": {"placement": "top_level", "subrecords": list(subrecords)},
                }],
            }
            path.write_text(json.dumps(document), encoding="utf-8")
            return record_schema_codegen.render(record_schema_codegen.load_constraints(path))[0]

    def test_a_schema_without_pairs_renders_a_valid_empty_array(self):
        rendered = self.render({"signature": "DATA", "minimum": 0, "maximum": 1})
        self.assertIn("inline constexpr std::array<SubrecordPairConstraint, 0> "
                      "k_generated_subrecord_pairs{};", rendered)
        self.assertNotIn("k_generated_subrecord_pairs[] = {}", rendered)

    def test_paired_rows_still_render_the_pair_table(self):
        rendered = self.render({"signature": "DATA", "minimum": 0, "maximum": 1, "paired_with": "GNAM"},
                               {"signature": "GNAM", "minimum": 0, "maximum": 1})
        self.assertIn("inline constexpr SubrecordPairConstraint k_generated_subrecord_pairs[] = {",
                      rendered)
        self.assertIn('Signature::from_string("DATA"), '
                      'Signature::from_string("GNAM")', rendered)


class RecordShardCodegenTests(unittest.TestCase):
    def render(self, record_count):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fields.json"
            records = [{
                "signature": f"R{index:03d}",
                "authoring": {"placement": "top_level", "subrecords": [
                    {"signature": "DATA", "minimum": 0, "maximum": 1},
                ]},
            } for index in range(record_count)]
            path.write_text(json.dumps({"version": 1, "records": records}), encoding="utf-8")
            return record_schema_codegen.render(record_schema_codegen.load_constraints(path))

    def test_a_long_rule_table_is_split_into_ordered_shards_within_the_line_limit(self):
        parent, shards = self.render(700)
        rules = sorted(name for name in shards if "subrecord_authoring_constraints" in name)
        self.assertEqual(len(rules), 2, rules)
        for name, text in shards.items():
            self.assertLessEqual(len(text.splitlines()), 750, name)
            self.assertTrue(text.startswith(record_schema_codegen.GENERATED_HEADER[0]))
        includes = [line for line in parent.splitlines() if line.startswith("#include ")]
        self.assertEqual(sorted(includes), sorted(f'#include "{name}"' for name in shards))
        self.assertEqual(includes[:2], [f'#include "{name}"' for name in rules])
        self.assertIn("Shard 1 of 2", shards[rules[0]])
        self.assertIn("records R000 to R599", shards[rules[0]])
        self.assertIn("Shard 2 of 2", shards[rules[1]])
        self.assertEqual(shards[rules[0]].count('Signature::from_string("DATA")'), 600)
        self.assertEqual(shards[rules[1]].count('Signature::from_string("DATA")'), 100)

    def test_a_short_rule_table_stays_inline(self):
        parent, shards = self.render(2)
        self.assertEqual(shards, {})
        self.assertIn('Signature::from_string("R001"), RecordPlacement::top_level, 1, 1', parent)


class RecordShardCheckTests(unittest.TestCase):
    """--check must fail on a shard that drifted or that the schema no longer produces."""

    def run_tool(self, *arguments):
        with mock.patch("sys.argv", ["record_schema_codegen.py", *arguments]), \
                contextlib.redirect_stdout(io.StringIO()) as captured:
            status = record_schema_codegen.main()
        return status, captured.getvalue()

    def written(self, directory, record_count=700):
        schema = Path(directory) / "fields.json"
        records = [{
            "signature": f"R{index:03d}",
            "authoring": {"placement": "top_level", "subrecords": [
                {"signature": "DATA", "minimum": 0, "maximum": 1},
            ]},
        } for index in range(record_count)]
        schema.write_text(json.dumps({"version": 1, "records": records}), encoding="utf-8")
        output = Path(directory) / "generated_record_constraints.inc"
        status, text = self.run_tool("--schema", str(schema), "--output", str(output))
        self.assertEqual((status, text), (0, ""))
        return schema, output

    def test_check_reports_a_stale_shard_and_the_writer_repairs_it(self):
        with tempfile.TemporaryDirectory() as directory:
            schema, output = self.written(directory)
            # Constraint: the record table shards too at 700 records, so pick the rule shards.
            shards = [path for path in record_schema_codegen.shard_files(output)
                      if "subrecord_authoring_constraints" in path.name]
            self.assertEqual(len(shards), 2, shards)
            drifted = shards[1]
            drifted.write_text(drifted.read_text(encoding="utf-8") + "  // drift\n", encoding="utf-8")

            status, text = self.run_tool("--schema", str(schema), "--output", str(output), "--check")
            self.assertEqual(status, 1)
            self.assertIn(f"stale: {drifted}", text)
            self.assertNotIn(f"stale: {output}", text)

            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output))[0], 0)
            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output),
                                           "--check"), (0, ""))

    def test_check_reports_a_stale_parent(self):
        with tempfile.TemporaryDirectory() as directory:
            schema, output = self.written(directory)
            output.write_text(output.read_text(encoding="utf-8").replace("#include", "// include"),
                              encoding="utf-8")
            status, text = self.run_tool("--schema", str(schema), "--output", str(output), "--check")
            self.assertEqual(status, 1)
            self.assertIn(f"stale: {output}", text)

    def test_check_reports_an_orphan_shard_and_the_writer_removes_it(self):
        with tempfile.TemporaryDirectory() as directory:
            schema, output = self.written(directory)
            orphan = output.with_name(
                "generated_record_constraints_subrecord_authoring_constraints_shard02.inc")
            orphan.write_text('  Signature::from_string("DATA"),\n', encoding="utf-8")

            status, text = self.run_tool("--schema", str(schema), "--output", str(output), "--check")
            self.assertEqual(status, 1)
            self.assertIn(f"orphan shard: {orphan}", text)

            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output))[0], 0)
            self.assertFalse(orphan.exists())
            self.assertEqual(self.run_tool("--schema", str(schema), "--output", str(output),
                                           "--check"), (0, ""))


if __name__ == "__main__":
    unittest.main()
