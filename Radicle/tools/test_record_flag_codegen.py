#!/usr/bin/env python3
"""Validation tests for record_flag_codegen.py, which turns schema record_flags into the k_record_flags table."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import record_flag_codegen  # noqa: E402

BS = chr(92)


def flag(path="WEAP" + BS + "Flags" + BS + "Foo", label="Foo", bit=3):
    return {"path": path, "label": label, "bit": bit}


class RecordFlagCodegenTests(unittest.TestCase):
    def load(self, *records, version=1):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fields.json"
            path.write_text(json.dumps({"version": version, "records": list(records)}), encoding="utf-8")
            return record_flag_codegen.load_rows(path)

    def weap(self, *flags):
        return {"signature": "WEAP", "record_flags": list(flags)}

    def test_rows_are_sorted_and_rendered_with_the_bit_mask(self):
        rows = self.load(self.weap(flag(bit=31, label="High", path="WEAP" + BS + "B"), flag(bit=0, label="Low")),
                         {"signature": "ARMO"})
        self.assertEqual([row["bit"] for row in rows], [0, 31])
        text = record_flag_codegen.render(rows)
        self.assertIn('0x00000001u}', text)
        self.assertIn('0x80000000u}', text)

    def test_malformed_flag_tables_are_rejected(self):
        bad = {
            "bit out of range": self.weap(flag(bit=32)),
            "bit is a bool": self.weap(flag(bit=True)),
            "duplicate bit": self.weap(flag(), flag(path="WEAP" + BS + "Other")),
            "duplicate path": self.weap(flag(bit=1), flag(bit=2)),
            "path not qualified": self.weap(flag(path="ARMO" + BS + "Foo")),
            "non-ascii label": self.weap(flag(label="café")),
            "extra key": self.weap({**flag(), "extra": 1}),
            "empty list": self.weap(),
        }
        for name, record in bad.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    self.load(record)

    def test_bad_document_and_signature_are_rejected(self):
        with self.assertRaises(ValueError):
            self.load(version=2)
        with self.assertRaises(ValueError):
            self.load({"signature": "WE", "record_flags": [flag()]})


if __name__ == "__main__":
    unittest.main()
