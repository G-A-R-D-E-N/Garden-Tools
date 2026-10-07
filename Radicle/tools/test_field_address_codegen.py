#!/usr/bin/env python3
"""Validation tests for the field addressing keys of schema_codegen.py and formlink_schema_codegen.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import formlink_schema_codegen  # noqa: E402
import schema_codegen  # noqa: E402

BS = chr(92)


def scalar(**overrides):
    row = {
        "path": "TEST" + BS + "Field",
        "label": "Field",
        "kind": "scalar",
        "subrecord": "DATA",
        "storage": "u32",
        "presentation": "identity",
        "minimum": 0,
        "maximum": 100,
        "insertion": "existing_only",
        "offset": 4,
    }
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def string(**overrides):
    row = {
        "path": "TEST" + BS + "Text",
        "label": "Text",
        "kind": "string",
        "subrecord": "STRG",
        "insertion": "existing_only",
    }
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def link(**overrides):
    row = {
        "path": "TEST" + BS + "Link",
        "label": "Link",
        "subrecord": "DATA",
        "offset": 0,
        "nullable": True,
        "insertion": "existing_only",
        "targets": ["STAT"],
    }
    row.update(overrides)
    return row


def write_schema(directory, fields=(), formlink_fields=()):
    document = {
        "version": 1,
        "records": [{"signature": "TEST", "fields": list(fields), "formlink_fields": list(formlink_fields)}],
        "target_unions": [{"name": "sigBaseObjects", "targets": ["STAT"]}],
    }
    path = Path(directory) / "fields.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


class SchemaCodegenTests(unittest.TestCase):
    def load(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_fields(write_schema(directory, fields))

    def link(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return formlink_schema_codegen.load_lists(write_schema(directory, formlink_fields=fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load(*fields)

    def test_signed_storages_accept_their_range_and_reject_beyond(self):
        self.assertEqual(self.load(scalar(storage="i8", minimum=-128, maximum=127))[0]["storage"], "i8")
        self.assertEqual(self.load(scalar(storage="i16", minimum=-32768, maximum=32767))[0]["storage"], "i16")
        self.rejects("exceed i8", scalar(storage="i8", minimum=-129, maximum=0))
        self.rejects("exceed i8", scalar(storage="i8", minimum=0, maximum=128))
        self.rejects("exceed i16", scalar(storage="i16", minimum=0, maximum=32768))

    def test_payload_size_requires_an_embedded_field_that_fits(self):
        self.assertEqual(self.load(scalar(payload_size=8))[0]["payload_size"], 8)
        self.rejects("embedded", scalar(offset=None, payload_size=8))
        self.rejects("smaller than the field end", scalar(payload_size=7))
        self.rejects("positive integer", scalar(payload_size=0))

    def test_repeated_and_stride_are_exclusive(self):
        self.rejects("mutually exclusive", scalar(repeated=True, stride=8))
        self.assertTrue(self.load(scalar(offset=None, repeated=True))[0]["address"]["repeated"])

    def test_stride_rules(self):
        self.assertEqual(self.load(scalar(stride=8, array_offset=4))[0]["address"]["stride"], 8)
        self.rejects("stride requires an offset", scalar(offset=None, stride=8))
        self.rejects("positive integer", scalar(stride=0))
        self.rejects("array_offset requires stride", scalar(array_offset=4))
        self.rejects("non-negative", scalar(stride=8, array_offset=-1))
        self.rejects("inside one stride element", scalar(stride=6))
        self.rejects("whole stride elements", scalar(stride=8, array_offset=4, payload_size=15))
        self.assertEqual(self.load(scalar(stride=8, array_offset=4, payload_size=20))[0]["payload_size"], 20)

    def test_when_rules(self):
        when = {"offset": 0, "storage": "u8", "equals": [1, 2]}
        self.assertEqual(self.load(scalar(when=when))[0]["address"]["when"]["equals"], [1, 2])
        self.assertEqual(self.load(scalar(when=dict(when, subrecord="CNAM")))[0]["address"]["when"]["subrecord"], "CNAM")
        self.rejects("unknown when keys", scalar(when=dict(when, extra=1)))
        self.rejects("when.storage", scalar(when=dict(when, storage="f32")))
        self.rejects("non-empty array", scalar(when=dict(when, equals=[])))
        self.rejects("at most 8", scalar(when=dict(when, equals=list(range(9)))))
        self.rejects("duplicate", scalar(when=dict(when, equals=[1, 1])))
        self.rejects("exceeds u8", scalar(when=dict(when, equals=[256])))
        self.rejects("exceeds u8", scalar(when=dict(when, equals=[-1])))
        self.rejects("integers", scalar(when=dict(when, equals=[True])))
        self.rejects("when.offset", scalar(when=dict(when, offset=-1)))
        self.rejects("own subrecord", scalar(when=dict(when, subrecord="DATA")))
        self.rejects("four printable", scalar(when=dict(when, subrecord="TOOLONG")))
        self.rejects("must be an object", scalar(when=[1]))
        wide = {"offset": 6, "storage": "u32", "equals": [1]}
        self.rejects("inside one stride element", scalar(offset=0, stride=8, when=wide))
        self.rejects("inside payload_size", scalar(offset=0, payload_size=8, when=wide))
        self.assertEqual(len(self.load(scalar(offset=0, stride=8, when=dict(wide, subrecord="CNAM")))), 1)
        self.assertEqual(len(self.load(scalar(offset=0, stride=12, when=wide))), 1)
        signed = self.load(scalar(when={"offset": 0, "storage": "i32", "equals": [-1]}))
        self.assertEqual(signed[0]["address"]["when"]["equals"], [-1])
        absent = self.load(scalar(when=dict(when, subrecord="CNAM", match_absent=True)))
        self.assertTrue(absent[0]["address"]["when"]["match_absent"])
        self.assertFalse(self.load(scalar(when=when))[0]["address"]["when"]["match_absent"])
        self.rejects("requires when.subrecord", scalar(when=dict(when, match_absent=True)))
        self.rejects("match_absent must be boolean", scalar(when=dict(when, subrecord="CNAM", match_absent=1)))

    def test_string_fields(self):
        field = self.load(string(repeated=True))[0]
        self.assertEqual((field["storage"], field["address"]["repeated"]), ("zstring", True))
        self.rejects("do not accept", string(storage="u32"))
        self.rejects("do not accept", string(offset=0))
        self.rejects("do not accept", string(stride=4))
        self.rejects("unsupported field kind", string(kind="blob"))
        self.rejects("insertion", string(insertion="always"))

    def test_discriminated_variants_may_share_bytes_but_plain_overlap_is_rejected(self):
        a = scalar(path="TEST" + BS + "A", offset=0, when={"offset": 0, "storage": "u8", "equals": [1]})
        b = scalar(path="TEST" + BS + "B", offset=0, when={"offset": 0, "storage": "u8", "equals": [2]})
        self.assertEqual(len(self.load(a, b)), 2)
        plain_a = scalar(path="TEST" + BS + "A", offset=0)
        plain_b = scalar(path="TEST" + BS + "B", offset=2)
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.load(plain_a, plain_b)

    def test_rendering_includes_the_address(self):
        fields = self.load(scalar(stride=8, array_offset=4, when={"offset": 0, "storage": "u8", "equals": [1, 3]}))
        text = schema_codegen.render(fields)
        self.assertIn("FieldAddress{false, 8, 4, FieldDiscriminator{Signature{}, 0, DiscriminatorStorage::u8, 2, {{1, 3}}, false}}", text)
        plain = schema_codegen.render(self.load(scalar()))
        self.assertIn("FieldAddress{}", plain)
        absent = schema_codegen.render(self.load(scalar(when={"subrecord": "CNAM", "offset": 0, "storage": "u32",
                                                              "equals": [7], "match_absent": True})))
        self.assertIn("DiscriminatorStorage::u32, 1, {{7}}, true}", absent)

    def test_formlink_fields_share_the_rules(self):
        rows, _nested, fields, _unions = self.link(link(repeated=True))[0:4]
        self.assertTrue(fields[0]["address"]["repeated"])
        with self.assertRaisesRegex(ValueError, "mutually exclusive"):
            self.link(link(repeated=True, stride=8))
        with self.assertRaisesRegex(ValueError, "inside one stride element"):
            self.link(link(offset=6, stride=8))
        with self.assertRaisesRegex(ValueError, "when.equals"):
            self.link(link(when={"offset": 0, "storage": "u8", "equals": []}))
        rendered = formlink_schema_codegen.render(*self.link(link(stride=8, array_offset=4)))
        self.assertIn("FieldAddress{false, 8, 4, FieldDiscriminator{}}", rendered)
        self.assertEqual(rows, [])


if __name__ == "__main__":
    unittest.main()
