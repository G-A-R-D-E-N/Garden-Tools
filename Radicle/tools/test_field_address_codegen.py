#!/usr/bin/env python3
"""Validation tests for the field addressing keys and the enum and flags names of schema_codegen.py and formlink_schema_codegen.py."""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import field_address_codegen  # noqa: E402
import formlink_schema_codegen  # noqa: E402
import schema_codegen  # noqa: E402
import schema_output  # noqa: E402

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


def named(presentation, *entries, **overrides):
    return scalar(presentation=presentation, names=list(entries), **overrides)


def enum_entry(number, name):
    return {"value": number, "name": name}


def flag_entry(number, name):
    return {"bit": number, "name": name}


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


def member(name, offset, **overrides):
    row = {"path": "TEST" + BS + name, "label": name, "offset": offset, "storage": "u32",
           "presentation": "identity", "minimum": 0, "maximum": 100}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def counted(members=None, **overrides):
    row = {"kind": "counted_array", "path": "TEST" + BS + "Items", "label": "Items", "subrecord": "DATA",
           "count_subrecord": "CNTA", "stride": 8, "insertion": "existing_only",
           "members": members if members is not None else [member("ItemA", 0), member("ItemB", 4)]}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def count_row(**overrides):
    row = {"path": "TEST" + BS + "Count", "label": "Count", "subrecord": "CNTA", "offset": None,
           "maximum": 0xFFFFFFFF}
    row.update(overrides)
    return scalar(**row)



def write_schema(directory, fields=(), formlink_fields=(), formlink_lists=(),
                 nested_formlink_lists=(), keyword_records=()):
    document = {
        "version": 1,
        "records": [{"signature": "TEST", "fields": list(fields), "formlink_fields": list(formlink_fields),
                     "formlink_lists": list(formlink_lists), "nested_formlink_lists": list(nested_formlink_lists)}],
        "keyword_records": list(keyword_records),
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

    def link_schema(self, **schema):
        with tempfile.TemporaryDirectory() as directory:
            return formlink_schema_codegen.load_lists(write_schema(directory, **schema))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load(*fields)

    def test_minimum_payload_size_for_optional_struct_members(self):
        field = self.load(scalar(minimum_payload_size=12))[0]
        self.assertEqual(field["minimum_payload_size"], 12)
        self.assertIn("FieldAddress{}, 12}", schema_codegen.render([field]))
        for value in (0, -1, True, "12", 7):
            with self.subTest(value=value):
                self.rejects("minimum_payload_size", scalar(minimum_payload_size=value))
        self.rejects("minimum_payload_size", scalar(offset=None, minimum_payload_size=12))
        self.rejects("minimum_payload_size", scalar(payload_size=8, minimum_payload_size=12))
        self.rejects("minimum_payload_size requires an embedded field", string(minimum_payload_size=12))

    def test_bytes_storage_covers_the_whole_payload_and_renders_as_hex(self):
        field = self.load(scalar(storage="bytes", offset=None, minimum=None, maximum=None))[0]
        self.assertEqual(field["storage"], "bytes")
        self.assertFalse(field["embedded"])
        self.assertIn("ScalarStorage::bytes", schema_codegen.render([field]))

    def test_bytes_storage_rejects_a_slot_a_presentation_and_names(self):
        self.rejects("bytes storage must cover the whole payload",
                     scalar(storage="bytes", offset=4))
        self.rejects("bytes storage requires identity presentation",
                     scalar(storage="bytes", offset=None, minimum=None, maximum=None,
                            presentation="enum", names=[enum_entry(1, "One")]))
        self.rejects("bytes storage does not accept names",
                     scalar(storage="bytes", offset=None, minimum=None, maximum=None,
                            names=[enum_entry(1, "One")]))

    def test_bytes_row_defaults_must_be_even_hex_and_are_not_applied(self):
        self.rejects("bytes row default must be hexadecimal",
                     scalar(storage="bytes", offset=None, minimum=None, maximum=None,
                            default="0g"))
        self.rejects("bytes row default must be an even-length hex string",
                     scalar(storage="bytes", offset=None, minimum=None, maximum=None,
                            default="abc"))
        self.rejects("bytes row default is not applied by any insertion policy",
                     scalar(storage="bytes", offset=None, minimum=None, maximum=None,
                            default="abcd"))

    def test_min_form_version_gates_a_row_and_rejects_bad_values(self):
        field = self.load(scalar(min_form_version=97))[0]
        self.assertEqual(field["min_form_version"], 97)
        self.assertIn("FieldAddress{}, 0, {}, 97}", schema_codegen.render([field]))
        for value in (0, -1, 65536, True, "97"):
            with self.subTest(value=value):
                self.rejects("min_form_version", scalar(min_form_version=value))

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

    def test_repeated_strided_rows_take_every_matching_subrecord(self):
        field = self.load(scalar(repeated=True, stride=8))[0]
        self.assertTrue(field["address"]["repeated"])
        self.assertEqual(field["address"]["stride"], 8)
        self.assertIn("FieldAddress{true, 8, 0, FieldDiscriminator{}}", schema_codegen.render([field]))
        self.rejects("stride requires an offset", scalar(repeated=True, offset=None, stride=8))
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
        self.rejects("exactly one", scalar(when=dict(when, not_equals=[1])))
        negated = self.load(scalar(when={"subrecord": "CNAM", "offset": 0, "storage": "u8", "not_equals": [1, 2]}))
        self.assertEqual(negated[0]["address"]["when"]["equals"], [1, 2])
        self.assertTrue(negated[0]["address"]["when"]["negated"])
        self.assertIn("FieldDiscriminator{Signature::from_string(\"CNAM\"), 0, DiscriminatorStorage::u8, 2, {{1, 2}}, false, true}",
                      schema_codegen.render(negated))
        self.rejects("when.not_equals must be", scalar(when={"offset": 0, "storage": "u8", "not_equals": []}))
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
        self.rejects("do not accept", string(stride=4))
        self.rejects("unsupported field kind", string(kind="blob"))
        self.rejects("insertion", string(insertion="always"))

    def test_flags_names_become_single_bit_masks(self):
        flags = self.load(named("flags", flag_entry(0, "First"), flag_entry(7, "Last"), storage="u8", maximum=255))[0]
        self.assertEqual(flags["names"], [{"value": 1, "name": "First"}, {"value": 128, "name": "Last"}])
        text = schema_codegen.render([flags])
        self.assertIn("ScalarPresentation::flags", text)
        self.assertIn('constexpr ScalarName k_scalar_names_0[] = {{1, "First"}, {128, "Last"}};', text)
        self.assertIn("FieldAddress{}, 0, std::span<const ScalarName>(k_scalar_names_0)}, false},", text)
        top = self.load(named("flags", flag_entry(31, "Top"), maximum=0xFFFFFFFF))[0]
        self.assertEqual(top["names"], [{"value": 2 ** 31, "name": "Top"}])

    def test_enum_names_keep_their_values(self):
        enum = self.load(named("enum", enum_entry(-1, "Auto"), enum_entry(2, "Two"),
                               storage="i8", minimum=-1, maximum=2))[0]
        self.assertEqual(enum["names"], [{"value": -1, "name": "Auto"}, {"value": 2, "name": "Two"}])
        text = schema_codegen.render([enum])
        self.assertIn("ScalarPresentation::enumeration", text)
        self.assertIn('= {{-1, "Auto"}, {2, "Two"}};', text)

    def test_identical_name_tables_are_emitted_once(self):
        entries = (enum_entry(0, "No"), enum_entry(1, "Yes"))
        text = schema_codegen.render(self.load(
            named("enum", *entries, offset=0),
            named("enum", *entries, offset=4, path="TEST" + BS + "Other")))
        self.assertEqual(text.count("constexpr ScalarName k_scalar_names_"), 1)
        self.assertEqual(text.count("std::span<const ScalarName>(k_scalar_names_0)"), 2)

    def test_rows_without_names_render_as_before(self):
        text = schema_codegen.render(self.load(scalar(), scalar(path="TEST" + BS + "Other", offset=8, minimum_payload_size=12)))
        self.assertNotIn("ScalarName", text.split("k_scalar_fields[]")[1])
        self.assertIn("FieldAddress{}, 12}, false},", text)

    def test_names_reject_duplicates(self):
        self.rejects("duplicate name", named("enum", enum_entry(0, "A"), enum_entry(1, "A")))
        self.rejects("duplicate value", named("enum", enum_entry(0, "A"), enum_entry(0, "B")))
        self.rejects("duplicate name", named("flags", flag_entry(1, "A"), flag_entry(2, "A")))
        self.rejects("duplicate bit", named("flags", flag_entry(1, "A"), flag_entry(1, "B")))

    def test_names_reject_values_outside_the_field(self):
        self.rejects("flag bit 8 exceeds u8", named("flags", flag_entry(8, "A"), storage="u8", maximum=255))
        self.rejects("flag bit 32 exceeds u32", named("flags", flag_entry(32, "A"), maximum=0xFFFFFFFF))
        self.rejects("flag bit -1 exceeds u32", named("flags", flag_entry(-1, "A")))
        self.rejects("enum value 256 exceeds u8", named("enum", enum_entry(256, "A"), storage="u8", maximum=255))
        self.rejects("enum value -1 exceeds u8", named("enum", enum_entry(-1, "A"), storage="u8", maximum=255))
        self.rejects("outside the field bounds", named("enum", enum_entry(101, "A")))
        self.rejects("outside the field bounds", named("flags", flag_entry(7, "A"), storage="u8", maximum=100))

    def test_names_need_an_integer_enum_or_flags_row(self):
        self.rejects("require the enum or flags", scalar(names=[enum_entry(0, "A")]))
        self.rejects("require the enum or flags", scalar(presentation="percent_0_100", names=[enum_entry(0, "A")]))
        self.rejects("non-empty names", scalar(presentation="enum"))
        self.rejects("non-empty names", named("flags"))
        self.rejects("integer storage", named("enum", enum_entry(0, "A"), storage="f32", minimum=0, maximum=1))
        self.rejects("unsigned storage", named("flags", flag_entry(0, "A"), storage="i32", minimum=-1, maximum=1))
        self.rejects("do not accept", string(names=[enum_entry(0, "A")]))

    def test_names_entries_are_checked(self):
        self.rejects("exactly 'bit'", named("flags", enum_entry(0, "A")))
        self.rejects("exactly 'value'", named("enum", {"value": 0}))
        self.rejects("exactly 'value'", named("enum", "A"))
        self.rejects("must be an integer", named("enum", enum_entry(True, "A")))
        self.rejects("must be an integer", named("flags", flag_entry(1.0, "A")))
        self.rejects("printable ASCII", named("enum", enum_entry(0, "caf" + chr(0xE9))))
        self.rejects("printable ASCII", named("enum", enum_entry(0, "")))
        self.rejects("printable ASCII", named("enum", enum_entry(0, 7)))

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
        self.assertIn("FieldAddress{false, 8, 4, FieldDiscriminator{Signature{}, 0, DiscriminatorStorage::u8, 2, {{1, 3}}, false, false}}", text)
        plain = schema_codegen.render(self.load(scalar()))
        self.assertIn("FieldAddress{}", plain)
        absent = schema_codegen.render(self.load(scalar(when={"subrecord": "CNAM", "offset": 0, "storage": "u32",
                                                              "equals": [7], "match_absent": True})))
        self.assertIn("DiscriminatorStorage::u32, 1, {{7}}, true, false}", absent)

    def test_guarded_and_unguarded_overlap_is_rejected(self):
        self.rejects("overlapping scalar fields in TEST.DATA",
                     scalar(when={"offset": 0, "storage": "u8", "equals": [1]}),
                     scalar(path="TEST" + BS + "Other"))

    def test_intersecting_discriminator_values_are_rejected(self):
        self.rejects("overlapping scalar fields in TEST.DATA",
                     scalar(when={"offset": 0, "storage": "u8", "equals": [1, 2]}),
                     scalar(path="TEST" + BS + "Other",
                            when={"offset": 0, "storage": "u8", "equals": [2, 3]}))

    def test_disjoint_discriminator_values_are_accepted(self):
        self.assertEqual(len(self.load(
            scalar(when={"offset": 0, "storage": "u8", "equals": [1]}),
            scalar(path="TEST" + BS + "Other",
                   when={"offset": 0, "storage": "u8", "equals": [2]}))), 2)

    def test_different_discriminators_are_not_exclusive(self):
        for changes in ({"offset": 1}, {"storage": "u16"}, {"subrecord": "CNAM"}):
            with self.subTest(changes=changes):
                self.rejects("overlapping scalar fields in TEST.DATA",
                             scalar(when={"offset": 0, "storage": "u8", "equals": [1]}),
                             scalar(path="TEST" + BS + "Other",
                                    when={"offset": 0, "storage": "u8", "equals": [2], **changes}))

    def test_not_equals_guards_are_exclusive_only_inside_their_complement(self):
        plain = {"offset": 0, "storage": "u8", "equals": [1]}
        self.assertEqual(len(self.load(scalar(when=plain),
                                       scalar(path="TEST" + BS + "Other",
                                              when={"offset": 0, "storage": "u8", "not_equals": [1, 2]}))), 2)
        self.rejects("overlapping scalar fields in TEST.DATA", scalar(when=plain),
                     scalar(path="TEST" + BS + "Other",
                            when={"offset": 0, "storage": "u8", "not_equals": [3, 4]}))
        self.rejects("overlapping scalar fields in TEST.DATA",
                     scalar(when={"offset": 0, "storage": "u8", "not_equals": [1]}),
                     scalar(path="TEST" + BS + "Other",
                            when={"offset": 0, "storage": "u8", "not_equals": [2]}))

    def test_different_layouts_are_rejected(self):
        for first, second in (({}, {"stride": 8}), ({}, {"repeated": True}),
                              ({"stride": 8}, {"stride": 12}),
                              ({"stride": 8}, {"stride": 8, "array_offset": 4})):
            with self.subTest(first=first, second=second):
                self.rejects("overlapping scalar fields in TEST.DATA",
                             scalar(**first), scalar(path="TEST" + BS + "Other", **second))

    def test_whole_payload_fields_conflict_with_embedded_fields(self):
        for whole in (string(subrecord="DATA"), scalar(offset=None)):
            with self.subTest(whole=whole):
                self.rejects("overlapping scalar fields in TEST.DATA", whole,
                             scalar(path="TEST" + BS + "Other"))

    def test_non_overlapping_embedded_fields_are_accepted(self):
        self.assertEqual(len(self.load(scalar(offset=0),
                                       scalar(path="TEST" + BS + "Other", offset=4))), 2)

    def test_exclusive_fields_may_use_different_layouts_or_whole_payloads(self):
        for second in (scalar(path="TEST" + BS + "Other", stride=8), string(subrecord="DATA")):
            with self.subTest(second=second):
                when = {"subrecord": "CNAM", "offset": 0, "storage": "u8"}
                self.assertEqual(len(self.load(
                    scalar(when=dict(when, equals=[1])),
                    dict(second, when=dict(when, equals=[2])))), 2)

    def test_disjoint_own_element_guards_do_not_exempt_different_layouts(self):
        self.rejects("conflicting.*layouts.*own-element",
                     scalar(offset=4, stride=8,
                            when={"offset": 0, "storage": "u8", "equals": [1]}),
                     scalar(path="TEST" + BS + "Other", offset=0, stride=4,
                            when={"offset": 0, "storage": "u8", "equals": [2]}))

    def test_every_pair_is_compared(self):
        self.rejects("overlapping scalar fields in TEST.DATA",
                     scalar(offset=0),
                     scalar(path="TEST" + BS + "Middle", offset=4),
                     scalar(path="TEST" + BS + "Other", offset=2,
                            when={"offset": 0, "storage": "u8", "equals": [1]}))

    def test_overlap_diagnostics_name_both_paths_and_reason(self):
        for second, reason in ((scalar(), "byte ranges"), (scalar(stride=8), "conflicting"),
                               (string(subrecord="DATA"), "whole-payload")):
            with self.subTest(second=second):
                with self.assertRaises(ValueError) as caught:
                    self.load(scalar(path="TEST" + BS + "First"),
                              dict(second, path="TEST" + BS + "Second"))
                message = str(caught.exception)
                self.assertIn(repr("TEST" + BS + "First"), message)
                self.assertIn(repr("TEST" + BS + "Second"), message)
                self.assertIn(reason, message)
                self.assertIn("unguarded", message)

    def test_formlink_fields_share_the_rules(self):
        rows, _nested, fields, _unions = self.link(link(repeated=True))[0:4]
        self.assertTrue(fields[0]["address"]["repeated"])
        strided = self.link(link(repeated=True, stride=8))[2][0]
        self.assertTrue(strided["address"]["repeated"])
        self.assertEqual(strided["address"]["stride"], 8)
        with self.assertRaisesRegex(ValueError, "inside one stride element"):
            self.link(link(offset=6, stride=8))
        with self.assertRaisesRegex(ValueError, "when.equals"):
            self.link(link(when={"offset": 0, "storage": "u8", "equals": []}))
        rendered, shards = formlink_schema_codegen.render(
            *self.link(link(stride=8, array_offset=4)))
        self.assertEqual(shards, {})
        self.assertIn("FieldAddress{false, 8, 4, FieldDiscriminator{}}", rendered)
        self.assertEqual(rows, [])

    def test_overlapping_formlink_fields_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping FormLink fields in TEST.DATA"):
            self.link(link(), link(path="TEST" + BS + "OtherLink", offset=2))

    def test_formlink_payload_sizes(self):
        rendered, shards = formlink_schema_codegen.render(
            *self.link(link(payload_sizes=[16, 32], stride=4)))
        self.assertEqual(shards, {})
        self.assertIn("{{16, 32}}", rendered)
        for sizes in ([], [4, 8, 12], [True], [0], [3], [0x100000000], [4, 4], "4"):
            with self.subTest(sizes=sizes), self.assertRaisesRegex(ValueError, "payload_sizes"):
                self.link(link(payload_sizes=sizes))
        with self.assertRaisesRegex(ValueError, "payload_sizes"):
            self.link(link(offset=8, payload_sizes=[8]))

    def test_formlink_and_scalar_overlap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping FormLink/scalar fields in TEST.DATA"):
            self.link_schema(fields=[scalar(offset=0)], formlink_fields=[link()])

    def test_formlink_and_string_overlap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping FormLink/scalar fields in TEST.DATA"):
            self.link_schema(fields=[string(subrecord="DATA")], formlink_fields=[link()])

    def test_formlink_list_owned_payload_is_rejected(self):
        lists = [{"path": "TEST" + BS + "Items", "label": "Items", "subrecord": "DATA",
                  "nullable": True, "mutation": "existing_neighbors", "targets": ["STAT"]}]
        nested = [{"container_path": "TEST" + BS + "Groups", "member": "Items", "label": "Items",
                   "subrecord": "DATA", "terminator": "STOP", "terminator_size": 0,
                   "nullable": True, "mutation": "terminated_group", "targets": ["STAT"]}]
        for schema in ({"formlink_lists": lists}, {"nested_formlink_lists": nested}):
            with self.subTest(schema=schema):
                with self.assertRaisesRegex(ValueError, "overlapping FormLink fields in TEST.DATA"):
                    self.link_schema(formlink_fields=[link()], **schema)

    def test_scalar_on_keyword_list_payload_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping FormLink/scalar fields in TEST.KWDA"):
            self.link_schema(fields=[scalar(subrecord="KWDA")], keyword_records=[{"record": "TEST"}])

    def test_guarded_formlink_and_unguarded_scalar_overlap_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "overlapping FormLink/scalar fields in TEST.DATA"):
            self.link_schema(fields=[scalar(offset=0)], formlink_fields=[
                link(when={"subrecord": "CNAM", "offset": 0, "storage": "u8", "equals": [1]})])

    def test_exclusive_formlink_and_scalar_overlap_is_accepted(self):
        when = {"subrecord": "CNAM", "offset": 0, "storage": "u8"}
        _lists, _nested, fields, _unions = self.link_schema(
            fields=[scalar(offset=0, when=dict(when, equals=[1]))],
            formlink_fields=[link(when=dict(when, equals=[2]))])
        self.assertEqual(len(fields), 1)

    def test_adjacent_formlink_and_scalar_fields_are_accepted(self):
        _lists, _nested, fields, _unions = self.link_schema(fields=[scalar(offset=4)], formlink_fields=[link()])
        self.assertEqual(len(fields), 1)

    def test_absent_matching_guards_are_not_exclusive(self):
        when = {"subrecord": "CNAM", "offset": 0, "storage": "u8", "match_absent": True}
        self.rejects("both when guards match an absent discriminator",
                     scalar(when=dict(when, equals=[1])),
                     scalar(path="TEST" + BS + "Other", when=dict(when, equals=[2])))

    def test_absent_matching_formlink_guards_are_not_exclusive(self):
        when = {"subrecord": "CNAM", "offset": 0, "storage": "u8", "match_absent": True}
        with self.assertRaisesRegex(ValueError, "both when guards match an absent discriminator"):
            self.link(link(when=dict(when, equals=[1])),
                      link(path="TEST" + BS + "OtherLink", when=dict(when, equals=[2])))

    def test_absent_matching_mixed_guards_are_not_exclusive(self):
        when = {"subrecord": "CNAM", "offset": 0, "storage": "u8", "match_absent": True}
        with self.assertRaisesRegex(ValueError, "both when guards match an absent discriminator"):
            self.link_schema(fields=[scalar(offset=0, when=dict(when, equals=[1]))],
                             formlink_fields=[link(when=dict(when, equals=[2]))])

    def test_one_absent_matching_guard_remains_exclusive(self):
        when = {"subrecord": "CNAM", "offset": 0, "storage": "u8"}
        self.assertEqual(len(self.load(
            scalar(when=dict(when, equals=[1], match_absent=True)),
            scalar(path="TEST" + BS + "Other", when=dict(when, equals=[2])))), 2)
        _lists, _nested, fields, _unions = self.link_schema(
            fields=[scalar(offset=0, when=dict(when, equals=[1], match_absent=True))],
            formlink_fields=[link(when=dict(when, equals=[2]))])
        self.assertEqual(len(fields), 1)

    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def test_counted_array_expands_to_stride_member_rows(self):
        fields, arrays, _groups = self.load_schema(counted(), count_row())
        self.assertEqual([(array["path"], array["stride"], array["count_subrecord"]) for array in arrays],
                         [("TEST" + BS + "Items", 8, "CNTA")])
        members = [field for field in fields if field["path"].startswith("TEST" + BS + "Item")]
        self.assertEqual([(field["offset"], field["address"]["stride"]) for field in members], [(0, 8), (4, 8)])
        text = schema_codegen.render(fields, arrays)
        self.assertIn("FieldAddress{false, 8, 0, FieldDiscriminator{}}", text)
        self.assertIn('{Signature::from_string("TEST"), "TEST' + BS + BS + 'Items", "Items", '
                      'Signature::from_string("DATA"), Signature::from_string("CNTA"), 8},', text)
        self.assertIn("constexpr std::array<CountedArrayDescriptor, 1> k_counted_arrays{{", text)

    def test_no_counted_arrays_renders_an_empty_table(self):
        self.assertIn("constexpr std::array<CountedArrayDescriptor, 0> k_counted_arrays{};",
                      schema_codegen.render(self.load(scalar())))

    def test_counted_array_members_must_fit_one_stride_element(self):
        self.rejects("inside one stride element",
                     counted(members=[member("ItemA", 0), member("ItemB", 6)]), count_row())
        self.rejects("stride requires an offset",
                     counted(members=[member("ItemA", 0), member("ItemB", None)]), count_row())

    def test_counted_array_members_must_not_overlap(self):
        self.rejects("overlapping scalar fields in TEST.DATA",
                     counted(members=[member("ItemA", 0), member("ItemB", 2)]), count_row())

    def test_counted_array_declaration_is_checked(self):
        self.rejects("unknown counted_array keys", counted(extra=1), count_row())
        self.rejects("stride must be a positive integer", counted(stride=0), count_row())
        self.rejects("stride must be a positive integer", counted(stride=True), count_row())
        self.rejects("qualified by TEST", counted(path="OTHER" + BS + "Items"), count_row())
        self.rejects("unsupported insertion policy", counted(insertion="always"), count_row())

    def test_counted_array_members_take_only_plain_scalar_keys(self):
        self.rejects("member keys", counted(members=[member("ItemA", 0, names=[enum_entry(0, "A")])]), count_row())
        self.rejects("member keys", counted(members=[member("ItemA", 0, stride=4)]), count_row())

    def test_counted_array_needs_one_whole_payload_u32_count_row(self):
        for count in (count_row(storage="u16", maximum=0xFFFF), count_row(storage="i32", maximum=0x7FFFFFFF),
                      count_row(offset=0)):
            with self.subTest(count=count):
                self.rejects("needs exactly one whole-payload u32 row", counted(), count)
        self.rejects("needs exactly one whole-payload u32 row", counted())

    def test_counted_array_subrecord_cannot_be_declared_twice(self):
        other = counted(path="TEST" + BS + "Other", label="Other",
                        members=[member("OtherA", 0), member("OtherB", 4)])
        self.rejects("declared twice", counted(), other, count_row())

    def test_counted_arrays_cannot_share_one_count_subrecord(self):
        other = counted(path="TEST" + BS + "Other", label="Other", subrecord="OTHR",
                        members=[member("OtherA", 0), member("OtherB", 4)])
        self.rejects("shared by two arrays", counted(), other, count_row())

    def test_uncounted_arrays_need_no_count_row_and_may_share_a_record(self):
        other = counted(path="TEST" + BS + "Other", label="Other", subrecord="OTHR", count_subrecord=None, members=[member("OtherA", 0)])
        fields, arrays, _groups = self.load_schema(counted(count_subrecord=None), other)
        self.assertEqual([array["count_subrecord"] for array in arrays], ["", ""])
        self.assertIn('Signature::from_string("DATA"), Signature{}, 8},', schema_codegen.render(fields, arrays))
        self.rejects("counted_array signatures must be four bytes", counted(count_subrecord=""), count_row())



class ControlByteSignatureTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def test_printable_signatures_render_as_string_literals(self):
        self.assertEqual(field_address_codegen.cpp_signature("DATA"),
                         'Signature::from_string("DATA")')

    def test_control_byte_signatures_render_as_packed_bytes(self):
        self.assertEqual(field_address_codegen.cpp_signature(chr(0) + "IAD"),
                         "Signature::from_bytes(0x44414900u)")
        self.assertEqual(field_address_codegen.cpp_signature(chr(0x10) + "IAD"),
                         "Signature::from_bytes(0x44414910u)")

    def test_counted_array_rows_accept_a_control_byte_signature(self):
        fields, arrays, _groups = self.load_schema(
            counted(subrecord=chr(0) + "IAD", count_subrecord=None,
                    members=[member("Time", 0), member("Value", 4)]))
        text = schema_codegen.render(fields, arrays)
        self.assertIn("Signature::from_bytes(0x44414900u)", text)
        self.assertEqual([field["subrecord"] for field in fields], [chr(0) + "IAD"] * 2)

    def test_signature_length_and_byte_range_still_bind(self):
        for value in ("DAT", "DATAA", "", chr(0x100) + "IAD"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "counted_array signatures"):
                    self.load_schema(counted(count_subrecord=None, subrecord=value))

    def test_group_successors_still_require_printable_signatures(self):
        group = {"kind": "subrecord_group", "path": "TEST" + BS + "Group", "label": "Group",
                 "insertion": "existing_only", "successors": [chr(0) + "IAD"],
                 "members": [{"subrecord": "DATA", "required": True, "payload_size": 4}]}
        with self.assertRaisesRegex(ValueError, "successors must be distinct four printable ASCII"):
            self.load_schema(group)

    def test_when_subrecord_still_requires_printable_signatures(self):
        when = {"subrecord": chr(0) + "IAD", "offset": 0, "storage": "u8", "equals": [0]}
        with self.assertRaisesRegex(ValueError, "when.subrecord must be four printable ASCII"):
            self.load_schema(scalar(when=when))


class ShardedScalarTableTests(unittest.TestCase):
    def test_rows_are_split_at_record_boundaries_under_the_budget(self):
        rows = [("A", f"a{index}") for index in range(4)] + [("B", f"b{index}") for index in range(4)]
        shards = schema_output.shard_rows(rows, budget=4)
        self.assertEqual([[line for _key, line in shard] for shard in shards],
                         [[f"a{index}" for index in range(4)], [f"b{index}" for index in range(4)]])
        self.assertEqual([line for _key, line in schema_output.shard_rows(rows, budget=100)[0]],
                         [line for _key, line in rows])

    def test_repository_table_is_sharded_below_the_source_line_limit(self):
        schema = Path(__file__).resolve().parent.parent / "schema" / "fallout4" / "fields.json"
        fields, arrays, groups = schema_codegen.load_schema(schema)
        inline = schema_codegen.render(fields, arrays, groups).split("\n")
        start = inline.index("constexpr ScalarFieldDescriptor k_scalar_fields[] = {")
        end = inline.index("};", start)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated_scalar_fields.inc"
            files = schema_output.render_output(fields, arrays, groups, output)
            for path, text in files.items():
                self.assertLessEqual(len(text.split("\n")) - 1, 750, path.name)
            shards = sorted(path for path in files if path != output)
            self.assertGreater(len(shards), 1)
            header = files[output].split("\n")
            for path in shards:
                self.assertIn(f'#include "{path.name}"', header)
            rows = []
            for path in shards:
                body = files[path].split("\n")[3:-1]
                rows.extend(body)
            self.assertEqual(rows, inline[start + 1:end])

    def test_emit_checks_writes_and_removes_stale_shards(self):
        schema = Path(__file__).resolve().parent.parent / "schema" / "fallout4" / "fields.json"
        fields, arrays, groups = schema_codegen.load_schema(schema)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "generated_scalar_fields.inc"

            def checked():
                with contextlib.redirect_stdout(io.StringIO()):
                    return schema_output.emit(fields, arrays, groups, output, check=True)

            self.assertEqual(checked(), 1)
            self.assertEqual(schema_output.emit(fields, arrays, groups, output, check=False), 0)
            self.assertEqual(checked(), 0)
            orphan = output.parent / "generated_scalar_fields_9.inc"
            orphan.write_text("// stale\n", encoding="utf-8")
            self.assertEqual(checked(), 1)
            schema_output.emit(fields, arrays, groups, output, check=False)
            self.assertFalse(orphan.exists())
            shard = sorted(output.parent.glob("generated_scalar_fields_[0-9]*.inc"))[0]
            shard.write_text(shard.read_text(encoding="utf-8") + "// drift\n", encoding="utf-8")
            self.assertEqual(checked(), 1)


if __name__ == "__main__":
    unittest.main()
