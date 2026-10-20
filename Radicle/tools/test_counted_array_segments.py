#!/usr/bin/env python3
"""Validation tests for header-counted counted_array segments in schema_codegen.py."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import schema_codegen  # noqa: E402
from test_field_address_codegen import member, write_schema  # noqa: E402

BS = chr(92)


def segment(name, count_word, index, stride, members=None, **overrides):
    row = {"kind": "counted_array", "path": "TEST" + BS + name, "label": name, "subrecord": "MODT",
           "count_word": count_word, "segment_index": index, "stride": stride,
           "insertion": "existing_only",
           "members": members if members is not None else [member(name + "Value", 0)]}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


class CountedArraySegmentTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def test_segments_render_their_counter_and_order(self):
        textures = segment("Textures", 0, 0, 12, [member("TextureHash", 0)])
        materials = segment("Materials", 3, 1, 12, [member("MaterialHash", 0)])
        fields, arrays, _groups = self.load_schema(textures, materials)
        self.assertEqual([(array["path"], array["segment"]) for array in arrays],
                         [("TEST" + BS + "Materials", {"count_word": 3, "index": 1, "count": 2}),
                          ("TEST" + BS + "Textures", {"count_word": 0, "index": 0, "count": 2})])
        self.assertEqual([field["address"]["segment"] for field in fields],
                         [{"count_word": 3, "index": 1, "count": 2},
                          {"count_word": 0, "index": 0, "count": 2}])
        text = schema_codegen.render(fields, arrays)
        self.assertIn("FieldAddress{false, 12, 0, FieldDiscriminator{}, SegmentAddress{3, 1, 2}}", text)
        self.assertIn("FieldAddress{false, 12, 0, FieldDiscriminator{}, SegmentAddress{0, 0, 2}}", text)
        self.assertIn('Signature::from_string("MODT"), Signature{}, 12, 3, 1, 2},', text)

    def test_segments_may_share_one_payload(self):
        fields, arrays, _groups = self.load_schema(segment("Textures", 0, 0, 12),
                                                   segment("Addons", 1, 1, 4))
        self.assertEqual(len(arrays), 2)
        self.assertEqual(len(fields), 2)

    def test_segment_members_carry_the_form_version_gate(self):
        fields, _arrays, _groups = self.load_schema(segment("Textures", 0, 0, 12, min_form_version=40))
        self.assertEqual([field["min_form_version"] for field in fields], [40])

    def test_segment_declaration_is_checked(self):
        self.rejects("segment keys must be declared together",
                     segment("Textures", 0, None, 12), segment("Addons", 1, 1, 4))
        self.rejects("count_word must index a header counter", segment("Textures", 8, 0, 12))
        self.rejects("count_word must index a header counter", segment("Textures", -1, 0, 12))
        self.rejects("segment_index must be below", segment("Textures", 0, 8, 12))
        self.rejects("reads its count from the payload header",
                     segment("Textures", 0, 0, 12, count_subrecord="CNTA"))
        self.rejects("min_form_version must be a 16-bit positive integer",
                     segment("Textures", 0, 0, 12, min_form_version=0))

    def test_segments_of_one_payload_must_be_complete(self):
        self.rejects("must be indexed 0..1", segment("Textures", 0, 0, 12), segment("Addons", 1, 2, 4))
        self.rejects("must read different header counters",
                     segment("Textures", 0, 0, 12), segment("Addons", 0, 1, 4))

    def test_an_unsegmented_array_still_may_not_share_its_subrecord(self):
        plain = {"kind": "counted_array", "path": "TEST" + BS + "Plain", "label": "Plain",
                 "subrecord": "MODT", "count_subrecord": "CNTA", "stride": 4,
                 "insertion": "existing_only", "members": [member("PlainValue", 0)]}
        count = {"path": "TEST" + BS + "Count", "label": "Count", "kind": "scalar",
                 "subrecord": "CNTA", "storage": "u32", "presentation": "identity",
                 "minimum": 0, "maximum": 0xFFFFFFFF, "insertion": "existing_only"}
        self.rejects("declared twice", plain, segment("Textures", 0, 0, 12), count)


if __name__ == "__main__":
    unittest.main()
