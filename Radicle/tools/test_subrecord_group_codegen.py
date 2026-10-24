#!/usr/bin/env python3
"""Validation tests for the subrecord_group rows of schema_codegen.py: members, successors, counted sub-runs and their rendered tables."""

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


def group_row(name, offset, **overrides):
    row = {"path": "TEST" + BS + name, "label": name, "offset": offset, "storage": "i32",
           "presentation": "identity", "minimum": 0, "maximum": 100, "insertion": "existing_only"}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def group_link(name, offset, **overrides):
    row = {"path": "TEST" + BS + name, "label": name, "offset": offset, "nullable": False,
           "insertion": "existing_only", "target_union": "sigBaseObjects"}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def group_member(signature, required=True, payload_size=8, fields=None, links=None, **overrides):
    row = {"subrecord": signature, "required": required, "repeatable": False, "payload_size": payload_size,
           "fields": [] if fields is None else fields, "formlink_fields": [] if links is None else links}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def group(members=None, **overrides):
    row = {"kind": "subrecord_group", "path": "TEST" + BS + "Items", "label": "Items", "count_subrecord": "COCT",
           "insertion": "existing_only",
           "members": [group_member("CNTO", default="0000000001000000", fields=[group_row("ItemCount", 4)],
                                    links=[group_link("ItemObject", 0)]),
                       group_member("COED", required=False, payload_size=12)] if members is None else members}
    row.update(overrides)
    return {k: v for k, v in row.items() if v is not None}


def group_run(count_member="VNAM", members=None):
    return {"count_member": count_member,
            "members": [group_member("WNAM", payload_size=0, default="00"),
                        group_member("YNAM", required=False, payload_size=2)] if members is None else members}


def run_group(**overrides):
    overrides.setdefault("count_subrecord", None)
    overrides.setdefault("members", [group_member("VNAM", payload_size=4)])
    overrides.setdefault("run", group_run())
    return group(**overrides)



class SubrecordGroupCodegenTests(unittest.TestCase):
    def load(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_fields(write_schema(directory, fields))

    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def link_schema(self, **schema):
        with tempfile.TemporaryDirectory() as directory:
            return formlink_schema_codegen.load_lists(write_schema(directory, **schema))

    def test_subrecord_group_expands_member_rows_and_renders_the_table(self):
        fields, arrays, groups = self.load_schema(group(), count_row(subrecord="COCT"))
        self.assertEqual(arrays, [])
        self.assertEqual([(item["path"], item["count_subrecord"], [member["subrecord"] for member in item["members"]])
                          for item in groups],
                         [("TEST" + BS + "Items", "COCT", ["CNTO", "COED"])])
        rows = [(field["path"], field["subrecord"], field["offset"], field["address"]["repeated"])
                for field in fields if field["path"].startswith("TEST" + BS + "Item")]
        self.assertEqual(rows, [("TEST" + BS + "ItemCount", "CNTO", 4, True)])
        text = schema_codegen.render(fields, arrays, groups)
        self.assertIn("constexpr std::uint8_t k_subrecord_group_default_0[] = "
                      "{0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00};", text)
        self.assertIn('{Signature::from_string("CNTO"), true, false, 8, '
                      'std::span<const std::uint8_t>(k_subrecord_group_default_0), false, false, false},', text)
        self.assertIn('("COED"), false, false, 12, std::span<const std::uint8_t>{}, false, false, false},', text)
        self.assertIn('{Signature::from_string("TEST"), "TEST' + BS + BS + 'Items", "Items", '
                      'Signature::from_string("COCT"), '
                      'std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_members_0), '
                      'std::span<const Signature>{}, SubrecordGroupRunDescriptor{}, false},', text)
        self.assertIn("constexpr std::array<SubrecordGroupDescriptor, 1> k_subrecord_groups{{", text)

    def test_subrecord_group_successors_render_as_a_signature_table(self):
        fields, arrays, groups = self.load_schema(group(successors=["DATA", "KSIZ"]), count_row(subrecord="COCT"))
        self.assertEqual(groups[0]["successors"], ["DATA", "KSIZ"])
        text = schema_codegen.render(fields, arrays, groups)
        self.assertIn('constexpr Signature k_subrecord_group_successors_0[] = '
                      '{Signature::from_string("DATA"), Signature::from_string("KSIZ")};', text)
        self.assertIn('std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_members_0), '
                      'std::span<const Signature>(k_subrecord_group_successors_0), '
                      'SubrecordGroupRunDescriptor{}, false},', text)

    def test_subrecord_group_successors_are_distinct_and_not_members(self):
        count = count_row(subrecord="COCT")
        self.rejects("successors must be distinct four printable ASCII", group(successors=["DATA", "DATA"]), count)
        self.rejects("successors must be distinct four printable ASCII", group(successors="DATA"), count)
        self.rejects("successors must be distinct four printable ASCII", group(successors=["DA"]), count)
        self.rejects("successors cannot be members or the count subrecord", group(successors=["CNTO"]), count)
        self.rejects("successors cannot be members or the count subrecord", group(successors=["COCT"]), count)

    def test_subrecord_group_shares_no_subrecord_with_a_counted_array(self):
        array = counted(subrecord="CNTO", path="TEST" + BS + "Array", label="Array", members=[member("ArrayA", 0)])
        self.rejects("shares a subrecord with counted_array", group(), array, count_row(subrecord="COCT"),
                     count_row(path="TEST" + BS + "ArrayCount", label="ArrayCount", subrecord="CNTA"))
        array = counted(subrecord="DATA", count_subrecord="COCT", path="TEST" + BS + "Array", label="Array",
                        members=[member("ArrayA", 0)])
        self.rejects("shares a subrecord with counted_array", group(), array, count_row(subrecord="COCT"))

    def test_no_subrecord_groups_renders_an_empty_table(self):
        self.assertIn("constexpr std::array<SubrecordGroupDescriptor, 0> k_subrecord_groups{};",
                      schema_codegen.render(self.load(scalar())))

    def test_subrecord_group_leader_is_first_required_and_non_repeatable(self):
        count = count_row(subrecord="COCT")
        self.rejects("leader must be required and non-repeatable",
                     group(members=[group_member("COED", required=False), group_member("CNTO")]), count)
        self.rejects("leader must be required and non-repeatable",
                     group(members=[group_member("CNTO", required=False)]), count)
        self.rejects("leader must be required and non-repeatable",
                     group(members=[group_member("CNTO", repeatable=True)]), count)

    def test_subrecord_group_members_are_distinct_and_exclude_the_count(self):
        count = count_row(subrecord="COCT")
        self.rejects("is repeated or is the count subrecord",
                     group(members=[group_member("CNTO"), group_member("CNTO", required=False)]), count)
        self.rejects("is repeated or is the count subrecord",
                     group(members=[group_member("CNTO"), group_member("COCT", required=False)]), count)

    def test_subrecord_group_rows_need_a_required_non_repeatable_member(self):
        count = count_row(subrecord="COCT")
        self.rejects("need a required, non-repeatable member",
                     group(members=[group_member("CNTO"),
                                    group_member("COED", required=False, fields=[group_row("Extra", 0)])]), count)
        self.rejects("need a required, non-repeatable member",
                     group(members=[group_member("CNTO", repeatable=True, fields=[group_row("ItemCount", 4)])]), count)

    def test_subrecord_group_rows_must_fit_the_member(self):
        count = count_row(subrecord="COCT")
        self.rejects("subrecord_group member field exceeds member size",
                     group(members=[group_member("CNTO", payload_size=6, fields=[group_row("ItemCount", 4)])]), count)
        self.rejects("subrecord_group member field exceeds member size",
                     group(members=[group_member("CNTO", fields=[group_row("ItemCount", None)])]), count)
        self.rejects("subrecord_group member field position is invalid",
                     group(members=[group_member("CNTO", fields=[group_row("ItemCount", -4)])]), count)
        self.rejects("subrecord_group member link exceeds member size",
                     group(members=[group_member("CNTO", payload_size=6, links=[group_link("ItemObject", 4)])]), count)
        self.rejects("subrecord_group member link position is invalid",
                     group(members=[group_member("CNTO", links=[group_link("ItemObject", None)])]), count)
        self.load_schema(group(members=[group_member("CNTO", payload_size=4, fields=[group_row("ItemCount", None)])]),
                         count)

    def test_subrecord_group_rows_take_only_plain_scalar_and_link_keys(self):
        count = count_row(subrecord="COCT")
        self.rejects("subrecord_group member field keys must not include",
                     group(members=[group_member("CNTO", fields=[group_row("ItemCount", 4, stride=4)])]), count)
        self.rejects("subrecord_group member field storage is unsupported",
                     group(members=[group_member("CNTO", fields=[group_row("ItemCount", 4, storage="zstring")])]),
                     count)
        self.rejects("subrecord_group member link keys must be a subset",
                     group(members=[group_member("CNTO", links=[group_link("ItemObject", 0, names=[])])]), count)
        self.rejects("subrecord_group member link keys must be a subset",
                     group(members=[group_member("CNTO", links=[group_link("ItemObject", 0, payload_sizes=[12])])]),
                     count)

    def test_subrecord_group_defaults_must_match_the_member_payload(self):
        count = count_row(subrecord="COCT")
        self.rejects("default must be 8 bytes", group(members=[group_member("CNTO", default="00")]), count)
        self.rejects("even-length hex string", group(members=[group_member("CNTO", default="0")]), count)
        self.rejects("even-length hex string", group(members=[group_member("CNTO", default="zz00000000000000")]), count)
        self.rejects("even-length hex string", group(members=[group_member("CNTO", default=7)]), count)

    def test_subrecord_group_declaration_is_checked(self):
        count = count_row(subrecord="COCT")
        self.rejects("unknown subrecord_group keys", group(extra=1), count)
        self.rejects("subrecord_group path must be qualified by TEST", group(path="OTHER" + BS + "Items"), count)
        self.rejects("subrecord_group label is invalid", group(label=""), count)
        self.rejects("subrecord_group count_subrecord must be four printable ASCII", group(count_subrecord="CO"), count)
        self.rejects("unsupported insertion policy", group(insertion="always"), count)
        self.rejects("subrecord_group members must be a non-empty array", group(members=[]), count)
        self.rejects("subrecord_group member keys must be a subset", group(members=[group_member("CNTO", extra=1)]),
                     count)
        self.rejects("subrecord_group member signature must be four bytes",
                     group(members=[group_member("CN")]), count)
        self.rejects("required and repeatable must be booleans",
                     group(members=[group_member("CNTO", required="yes")]), count)
        self.rejects("payload_size must be a non-negative integer",
                     group(members=[group_member("CNTO", payload_size=-1)]), count)
        self.rejects("subrecord_group member rows must be arrays", group(members=[group_member("CNTO", fields={})]),
                     count)

    def test_subrecord_group_count_row_must_be_one_whole_payload_u32(self):
        self.rejects("needs exactly one whole-payload u32 row", group())
        for count in (count_row(subrecord="COCT", storage="u16", maximum=0xFFFF),
                      count_row(subrecord="COCT", storage="i32", maximum=0x7FFFFFFF),
                      count_row(subrecord="COCT", offset=0)):
            with self.subTest(count=count):
                self.rejects("needs exactly one whole-payload u32 row", group(), count)

    def test_subrecord_group_count_subrecord_is_optional(self):
        self.assertIn('"Items", Signature{},', schema_codegen.render(*self.load_schema(group(count_subrecord=None))))
        self.rejects("subrecord_group count_subrecord must be four printable ASCII", group(count_subrecord=5))

    def test_subrecord_group_members_and_counts_cannot_be_shared(self):
        twin = group(path="TEST" + BS + "Other", label="Other", count_subrecord="OTHR",
                     members=[group_member("CNTO", fields=[group_row("TwinCount", 4)])])
        self.rejects("are declared by two groups", group(), twin, count_row(subrecord="COCT"),
                     count_row(path="TEST" + BS + "OtherCount", label="OtherCount", subrecord="OTHR"))
        other = group(path="TEST" + BS + "Other", label="Other",
                      members=[group_member("XTRA", fields=[group_row("OtherItem", 0)])])
        self.rejects("is shared by two groups", group(), other, count_row(subrecord="COCT"))

    def test_subrecord_group_count_cannot_be_another_groups_member(self):
        other = group(path="TEST" + BS + "Other", label="Other", count_subrecord="OTHR",
                      members=[group_member("COCT")])
        self.rejects("is a member of another group", group(), other, count_row(subrecord="COCT"),
                     count_row(path="TEST" + BS + "OtherCount", label="OtherCount", subrecord="OTHR"))

    def test_subrecord_group_run_expands_nested_rows_and_renders_the_table(self):
        fields, arrays, groups = self.load_schema(run_group())
        self.assertEqual((groups[0]["run"]["count_member"],
                          [member["subrecord"] for member in groups[0]["run"]["members"]]),
                         ("VNAM", ["WNAM", "YNAM"]))
        self.assertEqual([field for field in fields if field["subrecord"] == "YNAM"], [])
        text = schema_codegen.render(fields, arrays, groups)
        self.assertIn("constexpr SubrecordGroupMemberDescriptor k_subrecord_group_run_members_0[] = {", text)
        self.assertIn('{Signature::from_string("WNAM"), true, false, 0, '
                      'std::span<const std::uint8_t>(k_subrecord_group_default_0), false, false, false},', text)
        self.assertIn('std::span<const Signature>{}, '
                      'SubrecordGroupRunDescriptor{Signature::from_string("VNAM"), '
                      'std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_run_members_0)}, false},', text)

    def test_subrecord_group_without_a_run_renders_an_empty_descriptor(self):
        self.assertIn('std::span<const Signature>{}, SubrecordGroupRunDescriptor{}, false},',
                      schema_codegen.render(*self.load_schema(group(), count_row(subrecord="COCT"))))

    def test_subrecord_group_run_member_rows_are_expanded(self):
        fields, _arrays, _groups = self.load_schema(run_group(
            run=group_run(members=[group_member("WNAM", payload_size=4, fields=[group_row("NameIndex", 0)])])))
        rows = [(field["path"], field["subrecord"], field["offset"], field["address"]["repeated"])
                for field in fields if field["path"].startswith("TEST" + BS + "Name")]
        self.assertEqual(rows, [("TEST" + BS + "NameIndex", "WNAM", 0, True)])

    def test_subrecord_group_run_declaration_is_checked(self):
        self.rejects("subrecord_group run keys must be exactly", run_group(run="VNAM"))
        self.rejects("subrecord_group run keys must be exactly", run_group(run={"count_member": "VNAM"}))
        self.rejects("subrecord_group run keys must be exactly", run_group(run=dict(group_run(), extra=1)))
        self.rejects("subrecord_group run count_member must be four printable ASCII",
                     run_group(run=group_run(count_member="VN")))
        self.rejects("subrecord_group run count_member must be a four-byte non-repeatable member",
                     run_group(run=group_run(count_member="XTRA")))
        self.rejects("subrecord_group run count_member must be a four-byte non-repeatable member",
                     run_group(run=group_run(count_member="WNAM")))
        self.rejects("subrecord_group run members must be a non-empty array",
                     run_group(run=group_run(members=[])))
        self.rejects("subrecord_group run leader must be required and non-repeatable",
                     run_group(run=group_run(members=[group_member("WNAM", required=False, payload_size=0)])))
        self.rejects("is repeated or is the count subrecord",
                     run_group(run=group_run(members=[group_member("WNAM", payload_size=0),
                                                      group_member("VNAM", required=False, payload_size=4)])))
        self.rejects("is repeated or is the count subrecord",
                     run_group(successors=["DATA"],
                               run=group_run(members=[group_member("WNAM", payload_size=0),
                                                      group_member("DATA", required=False, payload_size=0)])))
        self.rejects("are declared by two groups",
                     run_group(), group(path="TEST" + BS + "Other", label="Other", count_subrecord=None,
                                        members=[group_member("XTRA", payload_size=4)],
                                        run=group_run(count_member="XTRA",
                                                      members=[group_member("WNAM", payload_size=0)])))
        self.rejects("shares a subrecord with counted_array", run_group(),
                     counted(subrecord="WNAM", path="TEST" + BS + "Array", label="Array", count_subrecord=None,
                             members=[member("ArrayA", 0)]))

    def test_subrecord_group_run_may_be_uncounted(self):
        fields, _arrays, groups = self.load_schema(
            group(count_subrecord=None, members=[group_member("NAM1", payload_size=0)],
                  run=group_run(count_member="", members=[
                      group_member("INDX", payload_size=4),
                      group_member("DATA", required=False, repeatable=True, payload_size=0),
                      group_member("INTV", payload_size=0)])))
        self.assertEqual(groups[0]["run"]["count_member"], "")
        text = schema_codegen.render(fields, _arrays, groups)
        self.assertIn('SubrecordGroupRunDescriptor{Signature{}, '
                      'std::span<const SubrecordGroupMemberDescriptor>(k_subrecord_group_run_members_0)},', text)
        self.assertIn('{Signature::from_string("DATA"), false, true, 0, std::span<const std::uint8_t>{}, '
                      'false, false, false},', text)

    def test_subrecord_group_run_members_cannot_repeat_a_group_member(self):
        count = count_row(subrecord="COCT")
        self.rejects("is repeated or is the count subrecord",
                     group(run=group_run(count_member="",
                                         members=[group_member("CNTO", payload_size=4)])), count)
        self.rejects("is repeated or is the count subrecord",
                     group(count_subrecord=None, members=[group_member("NAM1", payload_size=0)],
                           run=group_run(count_member="", members=[group_member("NAM1", payload_size=0)])))
        self.load_schema(group(run=group_run(count_member="")), count)

    def test_subrecord_group_links_join_the_formlink_table(self):
        _rows, _nested, fields, _unions = self.link_schema(fields=[group(), count_row(subrecord="COCT")])
        self.assertEqual([(row["path"], row["subrecord"], row["offset"], row["address"]["repeated"])
                          for row in fields],
                         [("TEST" + BS + "ItemObject", "CNTO", 0, True)])

    def test_subrecord_group_link_cannot_overlap_a_member_scalar(self):
        overlap = group(members=[group_member("CNTO", fields=[group_row("ItemCount", 0)],
                                              links=[group_link("ItemObject", 0)])])
        with self.assertRaisesRegex(ValueError, "overlapping FormLink/scalar fields in TEST.CNTO"):
            self.link_schema(fields=[overlap, count_row(subrecord="COCT")])
        self.link_schema(fields=[group(), count_row(subrecord="COCT")])


class LeadingGroupMemberTests(unittest.TestCase):
    def load_schema(self, *fields):
        with tempfile.TemporaryDirectory() as directory:
            return schema_codegen.load_schema(write_schema(directory, fields))

    def rejects(self, message, *fields):
        with self.assertRaisesRegex(ValueError, message):
            self.load_schema(*fields)

    def test_subrecord_group_leading_members_precede_a_required_leader(self):
        count = count_row(subrecord="COCT")
        _, _, groups = self.load_schema(
            group(members=[group_member("CS2K", required=False, payload_size=4, leading=True),
                           group_member("CNTO", payload_size=4)]), count)
        self.assertEqual([member["leading"] for member in groups[0]["members"]], [True, False])
        text = schema_codegen.render(*self.load_schema(
            group(members=[group_member("CS2K", required=False, payload_size=4, leading=True),
                           group_member("CNTO", payload_size=4)]), count))
        self.assertIn('{Signature::from_string("CS2K"), false, false, 4, std::span<const std::uint8_t>{}, '
                      'false, false, true},', text)
        self.assertIn('{Signature::from_string("CNTO"), true, false, 4, std::span<const std::uint8_t>{}, '
                      'false, false, false},', text)

    def test_subrecord_group_leading_members_must_be_optional_first_and_have_a_leader(self):
        count = count_row(subrecord="COCT")
        leading = {"required": False, "payload_size": 4, "leading": True}
        self.rejects("leading members must come first",
                     group(members=[group_member("CS2K", **dict(leading, required=True)), group_member("CNTO")]), count)
        self.rejects("leading members must come first",
                     group(members=[group_member("CS2K", repeatable=True, **leading), group_member("CNTO")]), count)
        self.rejects("leading members must come first",
                     group(members=[group_member("CNTO"), group_member("CS2K", **leading)]), count)
        self.rejects("leader must be required and non-repeatable",
                     group(members=[group_member("CS2K", **leading)]), count)
        self.rejects("leading must be a boolean",
                     group(members=[group_member("CS2K", **dict(leading, leading="yes")), group_member("CNTO")]), count)

    def test_subrecord_group_run_members_cannot_be_leading(self):
        leading_run = group_run(members=[group_member("WNAM", required=False, payload_size=2, leading=True),
                                         group_member("YNAM", payload_size=4)])
        self.rejects("subrecord_group run members cannot be leading", run_group(run=leading_run))

    def test_subrecord_group_requires_existing_count_renders_only_when_set(self):
        count = count_row(subrecord="COCT")
        plain = schema_codegen.render(*self.load_schema(group(), count))
        self.assertNotIn("true},", plain.split("k_subrecord_groups{{", 1)[1])
        flagged = schema_codegen.render(*self.load_schema(group(requires_existing_count=True), count))
        self.assertIn("k_subrecord_group_members_0), std::span<const Signature>{}, "
                      "SubrecordGroupRunDescriptor{}, true},", flagged)
        self.rejects("requires_existing_count must be a boolean on a counted group",
                     group(requires_existing_count=1), count)
        self.rejects("requires_existing_count must be a boolean on a counted group",
                     group(count_subrecord=None, requires_existing_count=True))


if __name__ == "__main__":
    unittest.main()
