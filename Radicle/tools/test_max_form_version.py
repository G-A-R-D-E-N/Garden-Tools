#!/usr/bin/env python3
"""Validation tests for max_form_version, the exclusive upper form-version bound of schema_codegen.py and formlink_schema_codegen.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import formlink_schema_codegen  # noqa: E402
import schema_codegen  # noqa: E402
from test_field_address_codegen import link, scalar, write_schema  # noqa: E402


class MaxFormVersionTests(unittest.TestCase):
    def load(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_fields(write_schema(directory, fields))

    def link(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return formlink_schema_codegen.load_lists(write_schema(directory, formlink_fields=fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load(*fields)

    def test_max_form_version_bounds_a_row_and_rejects_bad_values(self):
        field = self.load(scalar(max_form_version=152))[0]
        self.assertEqual(field["max_form_version"], 152)
        self.assertIn("FieldAddress{}, 0, {}, 0, false, 152}", schema_codegen.render([field]))
        self.assertEqual(self.load(scalar(min_form_version=97, max_form_version=152))[0]["max_form_version"], 152)
        for value in (0, -1, 65536, True, "152"):
            with self.subTest(value=value):
                self.rejects("max_form_version", scalar(max_form_version=value))
        for minimum, maximum in ((112, 112), (112, 97)):
            with self.subTest(minimum=minimum, maximum=maximum):
                self.rejects("must exceed min_form_version",
                             scalar(min_form_version=minimum, max_form_version=maximum))

    def test_formlink_max_form_version_bounds_a_row(self):
        rendered, shards = formlink_schema_codegen.render(
            *self.link(link(payload_sizes=[8], stride=8, max_form_version=152)))
        self.assertEqual(shards, {})
        self.assertIn("{{8}}, 152}", rendered)
        bare, _shards = formlink_schema_codegen.render(*self.link(link(max_form_version=152)))
        self.assertIn("{{}}, 152}", bare)
        for value in (0, -1, 65536, True, "152"):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "max_form_version"):
                self.link(link(max_form_version=value))


if __name__ == "__main__":
    unittest.main()
