#!/usr/bin/env python3
"""Validation tests for the storage and explicit anchor keys of formlink_lists rows in formlink_schema_codegen.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import formlink_schema_codegen  # noqa: E402


def load(row):
    document = {
        "version": 1,
        "records": [{"signature": "TEST", "fields": [], "formlink_lists": [row]}],
        "keyword_records": [],
        "target_unions": [{"name": "sigBaseObjects", "targets": ["STAT"]}],
    }
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fields.json"
        path.write_text(json.dumps(document), encoding="utf-8")
        return formlink_schema_codegen.load_lists(path)[0]


def row(**overrides):
    base = {
        "path": "TEST\\Links",
        "label": "Links",
        "subrecord": "PNAM",
        "targets": ["STAT"],
        "nullable": False,
        "mutation": "appendable",
    }
    base.update(overrides)
    return base


class FormLinkListStorageTests(unittest.TestCase):
    def test_storage_defaults_to_per_subrecord(self):
        self.assertEqual(load(row())[0]["storage"], "per_subrecord")

    def test_packed_array_storage_is_accepted(self):
        self.assertEqual(load(row(storage="packed_array"))[0]["storage"], "packed_array")

    def test_unknown_storage_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unsupported FormLink list storage"):
            load(row(storage="bogus"))

    def test_explicit_anchor_predecessors_replace_sibling_order(self):
        anchored = load(row(storage="packed_array", anchor_predecessors=["EDID"]))[0]
        self.assertEqual(anchored["anchor_predecessors"], ["EDID"])
        self.assertEqual(anchored["anchor_successors"], [])

    def test_explicit_anchor_must_be_signatures(self):
        with self.assertRaisesRegex(ValueError, "anchor_predecessors must be signatures"):
            load(row(anchor_predecessors=["EDIDX"]))


if __name__ == "__main__":
    unittest.main()
