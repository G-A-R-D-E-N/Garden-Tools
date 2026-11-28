#!/usr/bin/env python3
"""Validation tests for count-prefixed run arrays in schema_codegen.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema_codegen  # noqa: E402
from test_field_address_codegen import member, write_schema  # noqa: E402

BS = chr(92)
KEYWORDS = {"count_offset": 15, "count_width": 1, "stride": 4}


def run_array(name, stride, run, members=None, **overrides):
    row = {"kind": "counted_array", "path": "TEST" + BS + name, "label": name, "subrecord": "OBTS",
           "stride": stride, "repeated": True, "insertion": "existing_only", "run": run,
           "members": members if members is not None else [member(name + "Value", 0, storage="u8")]}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def ranks(**overrides):
    run = overrides.pop("run", {"start": 16, "before": [KEYWORDS]})
    return run_array("Ranks", 2, run, **overrides)


def includes(**overrides):
    return run_array("Includes", 7, {"start": 16, "before": [KEYWORDS, {"stride": 2}],
                                     "count_offset": 0, "count_width": 4}, **overrides)


class RunRowTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def test_run_rows_render_their_sections_and_skip_the_counted_array_table(self):
        fields, arrays, _groups = self.load_schema(ranks(), includes())
        self.assertEqual(arrays, [])
        by_path = {field["path"]: field["address"]["run"] for field in fields}
        self.assertEqual(by_path["TEST" + BS + "RanksValue"],
                         {"start": 16, "count": 2, "index": 1,
                          "sections": [{"count_offset": 15, "count_width": 1, "stride": 4},
                                       {"count_offset": 0, "count_width": 0, "stride": 2}]})
        self.assertEqual(by_path["TEST" + BS + "IncludesValue"]["index"], 2)
        text = schema_codegen.render(fields, arrays)
        self.assertIn("RunAddress{16, 2, 1, {{{15, 1, 4}, {0, 0, 2}}}}", text)
        self.assertIn("RunAddress{16, 3, 2, {{{15, 1, 4}, {0, 0, 2}, {0, 4, 7}}}}", text)
        self.assertIn("FieldAddress{true, 7, 0, FieldDiscriminator{}, SegmentAddress{}, RunAddress{", text)

    def test_run_declaration_is_checked(self):
        self.rejects("run keys must be a subset", ranks(run={"start": 16, "bogus": 1}))
        self.rejects("run start must be a 16-bit", ranks(run={"start": -1}))
        self.rejects("run start must be a 16-bit", ranks(run={"before": []}))
        self.rejects("count_offset and count_width must be declared together",
                     ranks(run={"start": 16, "count_offset": 0}))
        self.rejects("count_width must be 1, 2 or 4",
                     ranks(run={"start": 16, "count_offset": 0, "count_width": 3}))
        self.rejects("counts must lie in the fixed header",
                     ranks(run={"start": 4, "count_offset": 2, "count_width": 4}))
        self.rejects("run section stride must be a positive", ranks(
            run={"start": 16, "before": [{"count_offset": 15, "count_width": 1}]}))
        self.rejects("run before and after must be arrays of at most", ranks(
            run={"start": 16, "before": [{"stride": 1}] * 6}))
        self.rejects("run before and after must be arrays of at most", ranks(
            run={"start": 16, "before": [{"stride": 1}] * 3, "after": [{"stride": 1}] * 3}))

    def test_after_sections_extend_the_layout_past_the_row(self):
        after = [{"count_offset": 0, "count_width": 4, "stride": 7}]
        fields, _arrays, _groups = self.load_schema(ranks(run={"start": 16, "before": [KEYWORDS], "after": after}))
        run = fields[0]["address"]["run"]
        self.assertEqual((run["count"], run["index"]), (3, 1))
        self.assertEqual(run["sections"][2], {"count_offset": 0, "count_width": 4, "stride": 7})

    def test_inline_count_sections_render_the_sentinel_and_replace_the_count_keys(self):
        inline = {"count_inline": True, "stride": 4}
        fields, _arrays, _groups = self.load_schema(
            run_array("Tail", 7, {"start": 20, "before": [inline, {"count_inline": True, "stride": 8}],
                                  "count_offset": 0, "count_width": 4}, repeated=None))
        run = fields[0]["address"]["run"]
        self.assertEqual(run["sections"][:2], [{"count_offset": 65535, "count_width": 4, "stride": 4},
                                               {"count_offset": 65535, "count_width": 4, "stride": 8}])
        self.assertIn("RunAddress{20, 3, 2, {{{65535, 4, 4}, {65535, 4, 8}, {0, 4, 7}}}}",
                      schema_codegen.render(fields, []))
        self.rejects("count_inline must be true", ranks(run={"start": 16, "before": [{"count_inline": False, "stride": 4}]}))
        self.rejects("count_inline must be true", ranks(
            run={"start": 16, "before": [{"count_inline": True, "count_offset": 0, "count_width": 4, "stride": 4}]}))

    def test_an_inline_own_count_renders_the_sentinel_and_rejects_mixed_count_keys(self):
        fields, _arrays, _groups = self.load_schema(
            run_array("Slots", 4, {"start": 20, "count_inline": True, "after": [{"count_inline": True, "stride": 8}]},
                      repeated=None))
        run = fields[0]["address"]["run"]
        self.assertEqual((run["start"], run["count"], run["index"]), (20, 2, 0))
        self.assertEqual(run["sections"], [{"count_offset": 65535, "count_width": 4, "stride": 4},
                                           {"count_offset": 65535, "count_width": 4, "stride": 8}])
        self.assertIn("RunAddress{20, 2, 0, {{{65535, 4, 4}, {65535, 4, 8}}}}", schema_codegen.render(fields, []))
        self.rejects("count_inline must be true", ranks(run={"start": 16, "count_inline": False}))
        self.rejects("count_inline must be true", ranks(
            run={"start": 16, "count_inline": True, "count_offset": 0, "count_width": 4}))

    def test_repeated_requires_a_run(self):
        self.rejects("repeated requires a run", ranks(run=None))

    def test_run_layouts_of_one_payload_must_agree(self):
        moved = run_array("Moved", 2, {"start": 20, "before": [KEYWORDS]},
                          members=[member("MovedValue", 1, storage="u8")])
        self.rejects("run layouts of TEST.OBTS disagree", ranks(), moved)
        reshaped = run_array("Reshaped", 7, {"start": 16, "before": [{**KEYWORDS, "stride": 8}, {"stride": 2}],
                                             "count_offset": 0, "count_width": 4})
        self.rejects("run layouts of TEST.OBTS disagree", ranks(), reshaped)

    def test_run_rows_do_not_overlap_inside_one_section(self):
        self.rejects("overlapping scalar run fields",
                     ranks(members=[member("A", 0, storage="u16"), member("B", 1, storage="u8")]))


if __name__ == "__main__":
    unittest.main()
