#!/usr/bin/env python3
"""#602 D-1: the pinned definitions' conflict marks are inventoried, not dropped.

Regression: a mark that loses its element, or a site the scan stops seeing, turns
a benign or critical element back into a plain conflict, which is the
misclassification D-1 exists to prevent.
"""

import json
import subprocess
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
INVENTORY = HERE.parent / "schema" / "fallout4" / "xedit_conflict_marks.json"
XEDIT = Path.home() / "ds-proxy" / "xedit-src" / "Core" / "wbDefinitionsFO4.pas"


def load():
    return json.loads(INVENTORY.read_text(encoding="utf-8"))


class XeditConflictMarksTest(unittest.TestCase):
    def test_every_mark_site_is_recorded(self):
        document = load()
        self.assertEqual(document["total"], 146)
        self.assertEqual(document["definitions"], 145)
        self.assertEqual(document["runtime"], 1)
        self.assertEqual(
            document["priorities"],
            {"cpBenign": 109, "cpBenignIfAdded": 3, "cpCritical": 1, "cpIgnore": 32, "cpOverride": 1},
        )

    def test_the_one_conditional_assignment_is_kept_apart(self):
        document = load()
        (entry,) = document["runtime_rows"]
        self.assertEqual(entry["priority"], "cpIgnore")
        self.assertEqual(entry["line"], 2121)
        self.assertIn("aCP := cpIgnore", entry["declaration"])

    def test_inventory_records_the_pinned_revision(self):
        self.assertEqual(load()["xedit_revision"], "93cc0bc5a1251936c3c7859eee3150eda12a62d7")

    def test_known_marks_keep_their_element_and_priority(self):
        rows = load()["rows"]

        def find(**wanted):
            return [row for row in rows if all(row.get(key) == value for key, value in wanted.items())]

        self.assertTrue(find(signature="MPPC", priority="cpBenign"))
        self.assertTrue(find(signature="XLRL", priority="cpBenignIfAdded"))
        self.assertTrue(find(label="Version", priority="cpIgnore"))
        self.assertTrue(find(label="FragmentCount", priority="cpBenign"))
        self.assertTrue(find(label="Near Package Start Location", priority="cpIgnore"))
        self.assertEqual(len(find(priority="cpCritical")), 1)
        self.assertEqual(len(find(priority="cpOverride")), 1)

    def test_every_definition_names_its_element(self):
        for row in load()["rows"]:
            self.assertTrue(row["element"].startswith("wb"), row)
            self.assertTrue(row["signature"] or row["label"], row)

    @unittest.skipUnless(XEDIT.exists(), "pinned wbDefinitionsFO4.pas is not in ~/ds-proxy/xedit-src/Core")
    def test_checked_in_inventory_matches_the_pinned_definitions(self):
        result = subprocess.run(
            [sys.executable, str(HERE / "xedit_conflict_marks.py"),
             "--xedit", str(XEDIT), "--output", str(INVENTORY), "--check"],
            capture_output=True, text=True,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
