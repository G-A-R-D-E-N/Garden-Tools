#!/usr/bin/env python3
"""Validation and committed-output check for hkx_layout_codegen.py."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hkx_layout_codegen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LAYOUTS = ROOT / "data" / "hkx" / "HavokClassLayouts.json"
TYPES = ROOT / "data" / "hkx" / "HavokClassTypes.json"
OUTPUT = ROOT / "src" / "hkx" / "hkx_layouts.generated.inc"


def entry(size=16, members=(("field", 0),)):
    return {"size": size, "members": [list(member) for member in members]}


def sig(value="0x12345678"):
    return {"signature": value}


class CollectTests(unittest.TestCase):
    def test_rejects_a_class_without_a_signature(self):
        with self.assertRaisesRegex(ValueError, "no signature"):
            hkx_layout_codegen.collect({"hkA": entry()}, {"hkA": {}})

    def test_rejects_a_malformed_signature(self):
        with self.assertRaisesRegex(ValueError, "signature of the form"):
            hkx_layout_codegen.collect({"hkA": entry()}, {"hkA": sig("0x1234")})

    def test_rejects_a_negative_offset(self):
        with self.assertRaisesRegex(ValueError, "has offset"):
            hkx_layout_codegen.collect({"hkA": entry(members=[("field", -1)])}, {"hkA": sig()})

    def test_emits_only_classes_present_in_both_files(self):
        rows = hkx_layout_codegen.collect({"hkA": entry(), "hkB": entry()}, {"hkA": sig()})
        self.assertEqual([row[0] for row in rows], ["hkA"])


class RepositoryTests(unittest.TestCase):
    def test_committed_table_matches_the_radicle_owned_json(self):
        layouts = json.loads(LAYOUTS.read_text(encoding="utf-8"))["classes"]
        types = json.loads(TYPES.read_text(encoding="utf-8"))["classes"]
        rendered = hkx_layout_codegen.render(hkx_layout_codegen.collect(layouts, types))
        self.assertEqual(OUTPUT.read_text(encoding="utf-8"), rendered)
        self.assertNotIn("BGS/", rendered)


if __name__ == "__main__":
    unittest.main()
