#!/usr/bin/env python3
"""Tests for the tier, signature, row and typed module helpers of parity_matrix.py."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parity_matrix  # noqa: E402


class TierOf(unittest.TestCase):
    def test_tier_of_orders_evidence(self):
        self.assertEqual(parity_matrix.tier_of(1, 0, 0, False, [], 0, False), "L4")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, True, [], 0, False), "L4")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, False, [], 2, True), "L3")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, False, ["m"], 2, False), "L3")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, False, ["m"], 0, True), "L2")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, False, [], 0, True), "L1")
        self.assertEqual(parity_matrix.tier_of(0, 0, 0, False, [], 0, False), "L0")


class TierSummary(unittest.TestCase):
    def test_tier_summary_breaks_out_authoring_only_rows(self):
        self.assertEqual(parity_matrix.tier_summary({"L0": 2, "L4": 3}, [], []),
                         "L0 2, L1 0, L2 0, L3 0, L4 3")
        line = parity_matrix.tier_summary({"L4": 3}, ["DEBR", "NAVM"], ["NAVM"])
        self.assertIn("2 L4 rows have an authoring constraint and no scalar, string or FormLink field rows: "
                      "DEBR, NAVM", line)
        self.assertIn("FormLink list rows are also present on NAVM", line)


class MixedRecordSignatures(unittest.TestCase):
    def test_mixed_record_signatures_reads_quotes_sig_names_and_constants(self):
        text = ('constexpr auto k_weap = Signature::from_string("WEAP"); if (x == k_weap) {} '
                '"ARMO" sig_npc_ ')
        self.assertEqual(parity_matrix.mixed_record_signatures(text), {"WEAP", "ARMO", "NPC_"})


class RowExpansion(unittest.TestCase):
    def setUp(self):
        self.parse_counted_array = parity_matrix.schema_codegen.parse_counted_array
        self.parse_subrecord_group = parity_matrix.schema_codegen.parse_subrecord_group
        parity_matrix.schema_codegen.parse_counted_array = lambda f, s: (None, [{"kind": "scalar", "path": "c"}])
        parity_matrix.schema_codegen.parse_subrecord_group = (
            lambda f, s: (None, [{"kind": "scalar", "path": "g"}], [{"path": "l"}]))

    def tearDown(self):
        parity_matrix.schema_codegen.parse_counted_array = self.parse_counted_array
        parity_matrix.schema_codegen.parse_subrecord_group = self.parse_subrecord_group

    def test_scalar_rows_expands_counted_arrays_and_groups_via_schema_codegen(self):
        rows = parity_matrix.scalar_rows("ABCD", [{"kind": "counted_array"}, {"kind": "subrecord_group"},
                                                  {"kind": "scalar", "path": "s"}])
        self.assertEqual([row["path"] for row in rows], ["c", "g", "s"])
        record = {"formlink_fields": [{"path": "f"}], "fields": [{"kind": "subrecord_group"}]}
        self.assertEqual([row["path"] for row in parity_matrix.formlink_rows("ABCD", record)], ["f", "l"])


class TypedModules(unittest.TestCase):
    def setUp(self):
        self.typed_modules = parity_matrix.TYPED_MODULES

    def tearDown(self):
        parity_matrix.TYPED_MODULES = self.typed_modules

    def test_typed_modules_fail_closed_on_unknown_signature_or_missing_name(self):
        parity_matrix.TYPED_MODULES = {"src/forms/eyes.cpp": ["MISC"]}
        with self.assertRaisesRegex(SystemExit, "does not name MISC"):
            parity_matrix.typed_modules({"MISC"})
        parity_matrix.TYPED_MODULES = {"src/forms/eyes.cpp": ["ZZZZ"]}
        with self.assertRaisesRegex(SystemExit, "not in xedit_signatures.txt"):
            parity_matrix.typed_modules({"EYES"})


if __name__ == "__main__":
    unittest.main()
