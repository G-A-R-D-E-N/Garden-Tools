#!/usr/bin/env python3
"""Coverage guard tests for the xEdit CountPath inventory (#602 D-26)."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import xedit_count_coverage as coverage  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]


def count_label(entry):
    count = entry.get("count") or {}
    return count.get("subrecord") or count.get("path")


def site_index(entries, label, nth=0):
    return [index for index, entry in enumerate(entries) if count_label(entry) == label][nth]


class CountCoverageTests(unittest.TestCase):
    def setUp(self):
        self.entries = json.loads(
            (ROOT / "schema/fallout4/xedit_count_paths.json").read_text(encoding="utf-8"))["entries"]
        self.schema = json.loads(
            (ROOT / "schema/fallout4/fields.json").read_text(encoding="utf-8"))
        self.owners = coverage.counter_rows(self.schema)

    def site(self, label, nth=0):
        return self.entries[site_index(self.entries, label, nth)]

    def status(self, label, nth=0):
        return coverage.classify(self.site(label, nth), self.owners, ROOT)

    def test_every_count_site_is_classified(self):
        # Regression: a new xEdit count site must fail --strict instead of silently
# Constraint: entering the inventory without a schema owner or a named limitation.
        statuses = [coverage.classify(entry, self.owners, ROOT)[0] for entry in self.entries]
        self.assertEqual(len(self.entries), 31)
        self.assertNotIn("unclassified", statuses)
        self.assertEqual(statuses.count("mapped"), 31)
        self.assertEqual(statuses.count("blocked"), 0)
        # Regression: WTHR couples a plain VNAM string, which must not be read as
# Constraint: ownership of INNR's naming-rule count; the INNR group run owns it.
        self.assertEqual(statuses[site_index(self.entries, "VNAM")], "mapped")

    def test_coupled_authoring_and_formlink_counters_are_recognised(self):
# Constraint: Regression: the guard used to ignore coupled count subrecords, subrecord-group run counts and formlink counters.
        for label, nth in (("CITC", 0), ("OBTE", 0), ("ITMC", 0), ("LLCT", 0), ("LLCT", 1), ("LLCT", 2),
                           ("PRKZ", 0), ("BSIZ", 0), ("ISIZ", 0)):
            self.assertEqual(self.status(label, nth)[0], "mapped", (label, nth))
        self.assertEqual(self.status("..\\XCNT\\Swimming Count")[0], "mapped")  # FSTS XCNT via footstep formlink counters
        self.assertEqual(self.status("DATA\\Counter Effect Count")[0], "mapped")  # MGEF DATA via CounterEffects
        self.assertEqual(self.status("Meshes Count")[0], "mapped")  # CELL XCRI header-counted segments
        self.assertEqual(self.status("MPPC")[0], "mapped")  # RACE MorphGroups run count MPPC
        self.assertEqual(self.status("XRMR\\Linked Rooms Count")[0], "mapped")  # coupled REFR XRMR header/run
        self.assertEqual(self.status("Include Count", 1)[0], "mapped")  # OBTS include count, owned by object_template.cpp

    def test_vmad_fragment_count_is_owned_by_the_fragment_editor(self):
        # Regression: FragmentCount must read as mapped while the typed fragment editor owns the count.
        status, detail = self.status("FragmentCount")
        self.assertEqual(status, "mapped")
        self.assertIn("vmad_fragment_edits.cpp", detail)
        self.assertIn("indexed count word", detail)
        self.assertIn("flag-keyed", detail)

        # Regression: removing the fragment writers must not leave the count claimed as mapped by a line number.
        # Constraint: a tree without the owner code classifies the site as unclassified instead.
        entry = self.site("FragmentCount")
        with tempfile.TemporaryDirectory() as empty:
            self.assertEqual(coverage.classify(entry, self.owners, empty)[0], "unclassified")

        # Regression: the QUST count word must not stay owned by the PERK/TERM editor alone.
        # Constraint: each shape needs its own writer tokens, so dropping either one unclassifies the site.
        stubs = {
            "src/plugin/vmad_fragment_edits.cpp":
                "count_offset section.indexed fragment_flag_bits flags_offset",
            "src/plugin/vmad_writer.cpp": "edit.extras.count_offset",
            "src/schema/vmad.cpp": "walk_indexed_fragments walk_flagged_fragments",
        }
        with tempfile.TemporaryDirectory() as tree:
            for relative, text in stubs.items():
                path = Path(tree) / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding="utf-8")
            self.assertEqual(coverage.classify(entry, self.owners, tree)[0], "mapped")
            for missing in ("edit.extras.count_offset", "fragment_flag_bits", "section.indexed"):
                for relative, text in stubs.items():
                    (Path(tree) / relative).write_text(text.replace(missing, ""),
                                                       encoding="utf-8")
                self.assertEqual(coverage.classify(entry, self.owners, tree)[0],
                                 "unclassified", missing)

    def test_object_mod_and_template_counts_are_owned_by_their_writers(self):
        # Constraint: the OMOD property count must not be excluded as a D-32 script count, because the OMOD Properties run row sizes it.
        # Constraint: both OBTS count words stay mapped only while the row editor in object_template.cpp is in the tree, so the sites are owned by code rather than by an inventory line.
        status, detail = self.status("Property Count")
        self.assertEqual(status, "mapped")
        self.assertIn("run count", detail)
        self.assertNotIn("D-32", detail)
        for nth in (0, 1):
            status, detail = self.status("Include Count", nth)
            self.assertEqual(status, "mapped", nth)
            self.assertIn("insert_object_template_row", detail, nth)
            self.assertNotIn("D-32", detail, nth)

        # Regression: deleting the object-template row editor must unclassify its count sites instead of leaving them mapped.
        template_entry = self.site("Include Count")
        with tempfile.TemporaryDirectory() as tree:
            writer = Path(tree) / "src/forms/object_template.cpp"
            writer.parent.mkdir(parents=True)
            writer.write_text("insert_object_template_row remove_object_template_row next[15] next[count_offset + i]",
                              encoding="utf-8")
            self.assertEqual(coverage.classify(template_entry, self.owners, tree)[0], "mapped")
            writer.write_text("insert_object_template_row remove_object_template_row next[15]", encoding="utf-8")
            self.assertEqual(coverage.classify(template_entry, self.owners, tree)[0],
                             "unclassified")

        # Constraint: removing the OMOD Properties run row must unclassify the property count instead of leaving it mapped.
        entry = self.site("Property Count")
        stripped = {"records": [
            {**record, "fields": [row for row in record.get("fields") or [] if row.get("path") != "OMOD\\Properties"]}
            for record in self.schema["records"]]}
        self.assertEqual(coverage.classify(entry, coverage.counter_rows(stripped), ROOT)[0], "unclassified")

    def test_unowned_count_site_is_unclassified(self):
        # Regression: a count subrecord with no row and no coupling must not be
# Constraint: mistaken for a mapped site.
        entry = {"line": 0, "declaration": "", "model": "counted",
                 "count": {"kind": "subrecord", "subrecord": "ZZZZ"}}
        self.assertEqual(coverage.classify(entry, self.owners)[0], "unclassified")

    def test_every_leveled_list_couples_its_llct_counter(self):
# Constraint: Regression: LVSP listed LLCT as a plain editable subrecord, so a generic add or remove could desync the counter from its LVLO run.
        found = []
        for record in self.schema["records"]:
            for rule in (record.get("authoring") or {}).get("subrecords") or []:
                if rule["signature"] == "LLCT":
                    found.append(record["signature"])
                    self.assertTrue(rule.get("coupled", False), record["signature"])
        self.assertEqual(sorted(found), ["LVLI", "LVLN", "LVSP"])


if __name__ == "__main__":
    unittest.main()
