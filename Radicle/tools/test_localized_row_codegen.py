#!/usr/bin/env python3
"""Validation of the localized flag on string rows in schema_codegen.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
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


if __name__ == "__main__":
    unittest.main()
