#!/usr/bin/env python3

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from field_address_codegen import reject_overlaps  # noqa: E402


def entry(path, offset, min_form_version=0, max_form_version=0):
    return {
        "record": "TEST",
        "subrecord": "DATA",
        "path": path,
        "offset": offset,
        "embedded": True,
        "width": 4,
        "min_form_version": min_form_version,
        "max_form_version": max_form_version,
        "address": {"repeated": False, "stride": 0, "array_offset": 0, "when": None},
    }


class FormVersionWindowTests(unittest.TestCase):
    def accepts(self, *entries):
        reject_overlaps(list(entries), "scalar")

    def rejects(self, *entries):
        with self.assertRaisesRegex(ValueError, "overlapping scalar fields in TEST.DATA"):
            reject_overlaps(list(entries), "scalar")

    def test_disjoint_windows_may_share_bytes(self):
        self.accepts(entry("TEST\\Early", 0, max_form_version=60),
                     entry("TEST\\Middle", 0, min_form_version=60, max_form_version=104))
        self.accepts(entry("TEST\\Early", 0, max_form_version=60),
                     entry("TEST\\Late", 0, min_form_version=104))
        self.accepts(entry("TEST\\Late", 0, min_form_version=104),
                     entry("TEST\\Middle", 0, max_form_version=104))

    def test_overlapping_windows_still_reject(self):
        self.rejects(entry("TEST\\Early", 0, max_form_version=60), entry("TEST\\Plain", 0))
        self.rejects(entry("TEST\\Middle", 0, min_form_version=60),
                     entry("TEST\\PreCut", 0, max_form_version=104))
        self.rejects(entry("TEST\\Early", 0, max_form_version=60),
                     entry("TEST\\AlsoEarly", 0, max_form_version=60))
        self.rejects(entry("TEST\\Early", 0, max_form_version=104),
                     entry("TEST\\EarlyToo", 0, max_form_version=60))

    def test_separate_offsets_do_not_need_disjoint_windows(self):
        self.accepts(entry("TEST\\First", 0, max_form_version=60), entry("TEST\\Second", 4))

    def test_disjoint_windows_do_not_exempt_a_whole_payload_row(self):
        plain = entry("TEST\\Whole", 0, max_form_version=60)
        plain["embedded"] = False
        plain["width"] = 0
        self.rejects(entry("TEST\\Late", 0, min_form_version=104), plain)


if __name__ == "__main__":
    unittest.main()
