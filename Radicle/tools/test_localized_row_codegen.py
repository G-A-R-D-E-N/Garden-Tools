#!/usr/bin/env python3
"""Validation of the localized flag on string rows in schema_codegen.py and the record authoring tables in record_schema_codegen.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

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
            return record_schema_codegen.render(record_schema_codegen.load_constraints(path))

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


if __name__ == "__main__":
    unittest.main()
