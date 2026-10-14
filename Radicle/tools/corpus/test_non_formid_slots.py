#!/usr/bin/env python3
"""Tests for the static xEdit scalar pass of non_formid_slots.py."""

import sys
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
    def test_only_scalar_definitions_are_denied(self):
        self.assertEqual(non_formid_slots.static_scalar_pairs(RECORD, FORM_ID_SUBRECORDS), {("ABCD", "FNAM")})

    def test_code_after_the_last_record_is_not_part_of_it(self):
        self.assertEqual(non_formid_slots.static_scalar_pairs(RECORD + TRAILING, FORM_ID_SUBRECORDS),
                         {("ABCD", "FNAM")})


if __name__ == "__main__":
    unittest.main()
