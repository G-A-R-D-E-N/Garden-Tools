#!/usr/bin/env python3
"""Tests for the static xEdit scalar pass of non_formid_slots.py."""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import non_formid_slots  # noqa: E402

FORM_ID_SUBRECORDS = {"FNAM", "LNAM", "MNAM", "PNAM"}

RECORD = """
  wbRecord(ABCD, 'Scalar Record', [
    wbEDID,
    wbInteger(FNAM, 'Flags', itU32),
    wbFormIDCk(LNAM, 'Link', [ABCD]),
    wbInteger(MNAM, 'Count', itU8),
    wbFormIDCk(MNAM, 'Other Link', [ABCD])
  ]);
"""

TRAILING = """
  wbRecord(WXYZ, 'Last Record', [
    wbEDID
  ]);

  function wbStray: IwbDef;
  begin
    Result := wbInteger(PNAM, 'Stray', itU8);
  end;
"""


class StaticScalarPairs(unittest.TestCase):
    def setUp(self):
        non_formid_slots.HELPERS.clear()

    def test_only_scalar_definitions_are_denied(self):
        self.assertEqual(non_formid_slots.static_scalar_pairs(RECORD, FORM_ID_SUBRECORDS), {("ABCD", "FNAM")})

    def test_code_after_the_last_record_is_not_part_of_it(self):
        self.assertEqual(non_formid_slots.static_scalar_pairs(RECORD + TRAILING, FORM_ID_SUBRECORDS),
                         {("ABCD", "FNAM")})

    def test_struct_of_scalars_is_denied_and_struct_with_a_link_member_is_not(self):
        defs = """
  wbRecord(ABCD, 'R', [wbStruct(FNAM, 'S', [wbInteger('a', itU32), wbInteger('b', itU8)]),
    wbStruct(LNAM, 'T', [wbInteger('a', itU32), wbFormIDCk('b', [ABCD])])]);
"""
        self.assertEqual(non_formid_slots.static_scalar_pairs(defs, FORM_ID_SUBRECORDS), {("ABCD", "FNAM")})

    def test_helper_function_decides_the_slot(self):
        defs = """
function wbFlagsHelper: IwbDef;
begin
  Result := wbInteger(FNAM, 'F', itU32);
end;
function wbLinkHelper: IwbDef;
begin
  Result := wbFormIDCk(LNAM, 'L', [ABCD]);
end;
wbRecord(ABCD, 'R', [wbEDID, wbFlagsHelper, wbLinkHelper]);
"""
        self.assertEqual(non_formid_slots.static_scalar_pairs(defs, FORM_ID_SUBRECORDS), {("ABCD", "FNAM")})

    def test_two_records_with_one_signature_are_merged_so_any_link_definition_wins(self):
        defs = """
wbRecord(ABCD, 'Link', [wbFormIDCk(FNAM, 'Link', [ABCD])]);
wbRecord(ABCD, 'Scalar', [wbInteger(FNAM, 'Flags', itU32)]);
"""
        self.assertEqual(non_formid_slots.static_scalar_pairs(defs, FORM_ID_SUBRECORDS), set())

    def test_nested_brackets_do_not_end_a_record_early(self):
        defs = """
wbRecord(ABCD, 'R', [
  wbArray(FNAM, 'A', wbStruct('s', [wbInteger('a', itU8)])),
  wbFormIDCk(LNAM, 'L', [ABCD])
]);
"""
        self.assertEqual(non_formid_slots.static_scalar_pairs(defs, {"FNAM", "LNAM"}), {("ABCD", "FNAM")})
        self.assertEqual(non_formid_slots.bracket_span("x(a[b(c)])d", 1), "(a[b(c)])")
        self.assertEqual(non_formid_slots.bracket_span("x(a[b)c])d", 1), "(a[b)c]")


class CheckStatic(unittest.TestCase):
    def setUp(self):
        non_formid_slots.HELPERS.clear()

    def test_comments_are_stripped_before_matching(self):
        text = """
  wbRecord(ABCD, 'R', [
    { wbInteger(FNAM, 'x', itU8) }
    // wbInteger(FNAM, 'y', itU8)
    wbFormIDCk(LNAM, 'z', [ABCD])
  ]);
"""
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "defs.pas"
            path.write_text(text, encoding="utf-8")
            defs = non_formid_slots.load_defs([str(path)])
        self.assertEqual(non_formid_slots.static_scalar_pairs(defs, FORM_ID_SUBRECORDS), set())

    def test_check_static_reports_missing_pairs_and_succeeds_when_present(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            defs, ids, slots = root / "defs.pas", root / "ids.inc", root / "slots.inc"
            defs.write_text(RECORD, encoding="utf-8")
            ids.write_text("".join(f"0x{number:08X}u,  // {sig}\n" for number, sig in
                                   enumerate(["FNAM", "LNAM", "MNAM", "PNAM"], 1)), encoding="utf-8")
            slots.write_text("", encoding="utf-8")
            args = [str(defs), str(ids), str(slots)]
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                status = non_formid_slots.check_static(args)
            self.assertEqual(status, 1)
            self.assertEqual(out.getvalue(), "missing ABCD FNAM\n")
            slots.write_text('{Signature::from_string("ABCD"), Signature::from_string("FNAM")},\n', encoding="utf-8")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                status = non_formid_slots.check_static(args)
            self.assertEqual(status, 0)
            self.assertEqual(out.getvalue(), "")


if __name__ == "__main__":
    unittest.main()
