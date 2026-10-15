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


if __name__ == "__main__":
    unittest.main()
