#!/usr/bin/env python3
"""Validation and committed-output check for form_link_tables_codegen.py."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import form_link_tables_codegen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = ROOT / "schema" / "fallout4" / "fields.json"
OUTPUT = ROOT / "src" / "schema" / "generated"


def document(**overrides):
    value = {
        "formid_subrecords": ["ABCD"],
        "structured_formids": [{"signature": "WXYZ", "offset": 4, "min_size": 8}],
        "unclaimed_link_allowlist": [["REC4", "SUB4"]],
    }
    value.update(overrides)
    return value


class CollectTests(unittest.TestCase):
    def rejects(self, message, **overrides):
        with self.assertRaisesRegex(ValueError, message):
            form_link_tables_codegen.collect(document(**overrides))

    def test_accepts_a_minimal_document(self):
        form_link_tables_codegen.collect(document())

    def test_rejects_duplicate_signature(self):
        self.rejects("duplicate formid_subrecords", formid_subrecords=["ABCD", "ABCD"])

    def test_rejects_slot_that_does_not_fit_its_min_size(self):
        self.rejects("invalid structured_formids slot",
                     structured_formids=[{"signature": "WXYZ", "offset": 8, "min_size": 8}])

    def test_rejects_duplicate_slot(self):
        row = {"signature": "WXYZ", "offset": 0, "min_size": 4}
        self.rejects("duplicate structured_formids", structured_formids=[row, dict(row)])

    def test_rejects_bad_allowlist_pair(self):
        self.rejects("invalid unclaimed_link_allowlist", unclaimed_link_allowlist=[["TOOLONG", "SUB4"]])

    def test_rejects_missing_table(self):
        with self.assertRaisesRegex(ValueError, "must declare"):
            form_link_tables_codegen.collect({})


class RenderTests(unittest.TestCase):
    def test_packs_signatures_little_endian(self):
        files = form_link_tables_codegen.render(*form_link_tables_codegen.collect(document()))
        self.assertIn("0x44434241u,  // ABCD", files["formid_subrecords.inc"])
        self.assertIn("{0x5A595857u, 4, 8},  // WXYZ", files["structured_formids.inc"])
        self.assertIn('{"REC4", "SUB4"},', files["unclaimed_link_allowlist.inc"])


class RepositoryTests(unittest.TestCase):
    def test_committed_tables_match_fields_json(self):
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        files = form_link_tables_codegen.render(*form_link_tables_codegen.collect(schema))
        for name, text in files.items():
            self.assertEqual((OUTPUT / name).read_text(encoding="utf-8"), text, name)


if __name__ == "__main__":
    unittest.main()
