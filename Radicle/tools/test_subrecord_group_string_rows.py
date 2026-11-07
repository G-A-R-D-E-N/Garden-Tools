#!/usr/bin/env python3
"""Validation tests for the string member rows of a subrecord_group in schema_codegen.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema_codegen  # noqa: E402
from test_field_address_codegen import count_row, string, write_schema  # noqa: E402
from test_subrecord_group_codegen import group, group_member  # noqa: E402

BS = chr(92)


def string_member(**overrides):
    row = {"path": "TEST" + BS + "ItemText", "label": "Item text", "kind": "string", "offset": 0,
           "insertion": "existing_only"}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def text_group(row, signature="CNTO", payload_size=8):
    return group(members=[group_member(signature, payload_size=payload_size, fields=[row])])


class SubrecordGroupStringRowTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def load(self, *fields):
        return self.load_schema(*fields)[0]

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def test_member_string_row_expands_to_a_repeated_embedded_slot_row(self):
        fields = self.load(text_group(string_member()), count_row(subrecord="COCT"))
        rows = [field for field in fields if field["path"] == "TEST" + BS + "ItemText"]
        self.assertEqual(len(rows), 1)
        row = rows[0]
        self.assertEqual(row["storage"], "zstring")
        self.assertTrue(row["embedded"])
        self.assertEqual(row["offset"], 0)
        self.assertEqual((row["payload_size"], row["minimum_payload_size"]), (0, 0))
        self.assertTrue(row["address"]["repeated"])
        self.assertTrue(row["group_member"])

    def test_member_string_row_rejects_scalar_and_unknown_keys(self):
        count = count_row(subrecord="COCT")
        for overrides in ({"storage": "u32"}, {"presentation": "identity"}, {"minimum": 0},
                          {"maximum": 0}, {"names": []}):
            with self.subTest(overrides=overrides):
                self.rejects("subrecord_group member string rows do not accept",
                             text_group(string_member(**overrides)), count)
        for overrides in ({"payload_size": 8}, {"stride": 4}):
            with self.subTest(overrides=overrides):
                self.rejects("subrecord_group member field keys must not include",
                             text_group(string_member(**overrides)), count)
        self.rejects("subrecord_group member field kind is unsupported",
                     text_group(string_member(kind="blob")), count)
        self.rejects("subrecord_group member string row needs a non-negative offset",
                     text_group(string_member(offset=None)), count)
        self.rejects("subrecord_group member string row needs a non-negative offset",
                     text_group(string_member(offset=-4)), count)

    def test_member_string_row_does_not_conflict_with_a_record_level_row(self):
        fields = self.load(text_group(string_member()), string(subrecord="CNTO"),
                           count_row(subrecord="COCT"))
        self.assertEqual(len([field for field in fields if field["subrecord"] == "CNTO"]), 2)

    def test_member_string_row_still_conflicts_with_a_repeated_whole_payload_row(self):
        self.rejects("overlapping scalar fields in TEST.CNTO",
                     text_group(string_member()), string(subrecord="CNTO", repeated=True),
                     count_row(subrecord="COCT"))


if __name__ == "__main__":
    unittest.main()
