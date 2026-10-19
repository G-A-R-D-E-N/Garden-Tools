#!/usr/bin/env python3
"""Tests for the leading member flag of subrecord_group rows in schema_codegen.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema_codegen  # noqa: E402
from test_field_address_codegen import count_row, group, group_member, write_schema  # noqa: E402


class LeadingGroupMemberTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def test_subrecord_group_leading_members_precede_a_required_leader(self):
        count = count_row(subrecord="COCT")
        _, _, groups = self.load_schema(
            group(members=[group_member("CS2K", required=False, payload_size=4, leading=True),
                           group_member("CNTO", payload_size=4)]), count)
        self.assertEqual([member["leading"] for member in groups[0]["members"]], [True, False])
        text = schema_codegen.render(*self.load_schema(
            group(members=[group_member("CS2K", required=False, payload_size=4, leading=True),
                           group_member("CNTO", payload_size=4)]), count))
        self.assertIn('{Signature::from_string("CS2K"), false, false, 4, std::span<const std::uint8_t>{}, false, false, true},', text)
        self.assertIn('{Signature::from_string("CNTO"), true, false, 4, std::span<const std::uint8_t>{}, false, false, false},', text)

    def test_subrecord_group_leading_members_must_be_optional_first_and_have_a_leader(self):
        count = count_row(subrecord="COCT")
        leading = {"required": False, "payload_size": 4, "leading": True}
        self.rejects("leading members must come first",
                     group(members=[group_member("CS2K", **dict(leading, required=True)), group_member("CNTO")]), count)
        self.rejects("leading members must come first",
                     group(members=[group_member("CS2K", repeatable=True, **leading), group_member("CNTO")]), count)
        self.rejects("leading members must come first",
                     group(members=[group_member("CNTO"), group_member("CS2K", **leading)]), count)
        self.rejects("leader must be required and non-repeatable",
                     group(members=[group_member("CS2K", **leading)]), count)
        self.rejects("leading must be a boolean",
                     group(members=[group_member("CS2K", **dict(leading, leading="yes")), group_member("CNTO")]), count)


if __name__ == "__main__":
    unittest.main()
