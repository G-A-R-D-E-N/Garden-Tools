#!/usr/bin/env python3
"""Validation and committed-output check for hkx_layout_codegen.py."""

import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hkx_layout_codegen  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
LAYOUTS = ROOT / "data" / "hkx" / "HavokClassLayouts.json"
TYPES = ROOT / "data" / "hkx" / "HavokClassTypes.json"
OUTPUT = ROOT / "src" / "hkx" / "hkx_layouts.generated.inc"


def entry(size=16, members=(("field", 0),)):
    return {"size": size, "members": [list(member) for member in members]}


def sig(value="0x12345678"):
    return {"signature": value}


class CollectTests(unittest.TestCase):
    def test_rejects_a_class_without_a_signature(self):
        with self.assertRaisesRegex(ValueError, "no signature"):
            hkx_layout_codegen.collect({"hkA": entry()}, {"hkA": {}})

    def test_rejects_a_malformed_signature(self):
        with self.assertRaisesRegex(ValueError, "signature of the form"):
            hkx_layout_codegen.collect({"hkA": entry()}, {"hkA": sig("0x1234")})

    def test_rejects_a_negative_offset(self):
        with self.assertRaisesRegex(ValueError, "has offset"):
            hkx_layout_codegen.collect({"hkA": entry(members=[("field", -1)])}, {"hkA": sig()})

    def test_carries_member_type_parent_and_the_written_flag_into_the_table(self):
        # Regression: an inherited member or a runtime-only slot must not read as stored file data.
        layouts = {"hkA": entry(members=[]),
                   "hkB": {"parent": "hkA", "size": 24, "members": [["name", 16, "stringptr"], ["time", 20, "real"]]}}
        types = {"hkA": sig(),
                 "hkB": {"signature": "0x12345678",
                         "members": [typed("name", 16, "TYPE_STRINGPTR"), typed("time", 20, "TYPE_REAL", written=False)]}}
        rows = hkx_layout_codegen.collect(layouts, types)
        self.assertEqual(rows[1][3], [("name", 16, "String", "Other", "", "stringptr", True, None),
                                      ("time", 20, "Other", "Other", "", "real", False, None)])
        self.assertEqual(rows[1][4], "hkA")
        rendered = hkx_layout_codegen.render(rows)
        self.assertIn('{"time", 20u, K::Other, K::Other, "", "real", false, nullptr}', rendered)
        self.assertIn('{"hkB", 305419896u, 24u, k_members_hkB, "hkA"}', rendered)

    def test_emits_only_classes_present_in_both_files(self):
        rows = hkx_layout_codegen.collect({"hkA": entry(), "hkB": entry()}, {"hkA": sig()})
        self.assertEqual([row[0] for row in rows], ["hkA"])


def typed(name, offset, vtype, vsub="TYPE_VOID", ctype=None, written=True, arrsize=0):
    return {"name": name, "offset": offset, "vtype": vtype, "vsub": vsub, "ctype": ctype,
            "written": written, "arrsize": arrsize}


class MemberKindTests(unittest.TestCase):
    # Constraint: a runtime-only or unwalkable member typed as a value would make the value search read garbage.

    def kinds(self, members):
        layouts = {"hkA": entry(members=[(m["name"], m["offset"]) for m in members]),
                   "hkChild": entry(), "hkParent": entry()}
        layouts["hkChild"]["parent"] = "hkParent"
        types = {"hkA": {"signature": "0x12345678", "members": members},
                 "hkChild": sig(), "hkParent": sig()}
        rows = hkx_layout_codegen.collect(layouts, types)
        return {name: (kind, element, type_class) for name, _, kind, element, type_class, _, _, _ in
                next(row for row in rows if row[0] == "hkA")[3]}, rows

    def test_types_scalars_strings_enums_structs_and_arrays_and_skips_the_rest(self):
        kinds, _ = self.kinds([
            typed("speed", 0, "TYPE_REAL"),
            typed("label", 8, "TYPE_STRINGPTR"),
            typed("mode", 16, "TYPE_ENUM", "TYPE_INT8"),
            typed("event", 24, "TYPE_STRUCT", ctype="hkChild"),
            typed("rows", 40, "TYPE_ARRAY", "TYPE_STRUCT", ctype="hkChild"),
            typed("ids", 56, "TYPE_ARRAY", "TYPE_INT16"),
            typed("runtime", 72, "TYPE_REAL", written=False),
            typed("fixed", 76, "TYPE_REAL", arrsize=4),
            typed("link", 96, "TYPE_POINTER", "TYPE_STRUCT", ctype="hkChild"),
            typed("lost", 104, "TYPE_STRUCT", ctype="hkMissing"),
            typed("wide", 192, "TYPE_ULONG"),
            typed("grid", 200, "TYPE_MATRIX4"),
        ])
        self.assertEqual(kinds["speed"], ("Real", "Other", ""))
        self.assertEqual(kinds["label"], ("String", "Other", ""))
        self.assertEqual(kinds["mode"], ("Int8", "Other", ""))
        self.assertEqual(kinds["event"], ("Struct", "Other", "hkChild"))
        self.assertEqual(kinds["rows"], ("Array", "Struct", "hkChild"))
        self.assertEqual(kinds["ids"], ("Array", "Int16", ""))
        for name in ("runtime", "fixed", "link", "lost", "wide", "grid"):
            self.assertEqual(kinds[name], ("Other", "Other", ""), name)

    # Regression: vectors, matrices, half and relative arrays must be typed so the inspector and the value search read them, while ulong and matrix4 (no verified width) stay Other.
    def test_types_vectors_matrices_half_and_relative_arrays(self):
        kinds, _ = self.kinds([
            typed("quad", 0, "TYPE_VECTOR4"),
            typed("rot", 16, "TYPE_QUATERNION"),
            typed("basis", 32, "TYPE_ROTATION"),
            typed("damp", 80, "TYPE_HALF"),
            typed("planes", 88, "TYPE_RELARRAY", "TYPE_VECTOR4"),
            typed("faces", 92, "TYPE_RELARRAY", "TYPE_STRUCT", ctype="hkChild"),
            typed("pose", 96, "TYPE_ARRAY", "TYPE_QSTRANSFORM"),
        ])
        self.assertEqual(kinds["quad"], ("Vector4", "Other", ""))
        self.assertEqual(kinds["rot"], ("Quaternion", "Other", ""))
        self.assertEqual(kinds["basis"], ("Matrix3", "Other", ""))
        self.assertEqual(kinds["damp"], ("Half", "Other", ""))
        self.assertEqual(kinds["planes"], ("RelArray", "Vector4", ""))
        self.assertEqual(kinds["faces"], ("RelArray", "Struct", "hkChild"))
        self.assertEqual(kinds["pose"], ("Array", "QsTransform", ""))

    # Regression: an enum name must resolve through the class chain, then through a name only one class declares, and stay unnamed when several classes declare it with different values.
    def test_resolves_enum_tables_through_the_chain_then_a_unique_name(self):
        modes = {"MODE_A": 0, "MODE_B": 1}

        def klass(parent=None, enums=None, members=()):
            row = {"signature": "0x12345678", "members": list(members)}
            if parent:
                row["parent"] = parent
            if enums:
                row["enums"] = enums
            return row

        members = [typed("own", 0, "TYPE_ENUM", "TYPE_INT8"), typed("base", 1, "TYPE_ENUM", "TYPE_INT8"),
                   typed("elsewhere", 2, "TYPE_ENUM", "TYPE_INT8"), typed("clash", 3, "TYPE_ENUM", "TYPE_INT8")]
        for member, enum in zip(members, ("Own", "Base", "Remote", "Shared")):
            member["etype"] = enum
        types = {"hkBase": klass(enums={"Base": modes}),
                 "hkA": klass("hkBase", {"Own": {"OWN_X": 3}}, members),
                 "hkRemote": klass(enums={"Remote": {"REMOTE_X": 7}}),
                 "hkOne": klass(enums={"Shared": {"ONE": 1}}),
                 "hkTwo": klass(enums={"Shared": {"TWO": 2}})}
        layouts = {name: entry(members=[(m["name"], m["offset"]) for m in row["members"]]) for name, row in types.items()}
        layouts["hkA"]["parent"] = "hkBase"
        rows = {row[0]: row for row in hkx_layout_codegen.collect(layouts, types)}
        enums = {member[0]: member[7] for member in rows["hkA"][3]}
        self.assertEqual(enums["own"], ("hkA", "Own", ((3, "OWN_X"),)))
        self.assertEqual(enums["base"], ("hkBase", "Base", ((0, "MODE_A"), (1, "MODE_B"))))
        self.assertEqual(enums["elsewhere"], ("hkRemote", "Remote", ((7, "REMOTE_X"),)))
        self.assertIsNone(enums["clash"])
        rendered = hkx_layout_codegen.render(list(rows.values()))
        self.assertIn('{"MODE_A", 0}, {"MODE_B", 1}', rendered)
        self.assertIn('"Base", "hkBase", k_enum_values_', rendered)

    def test_keeps_a_parent_only_when_it_is_in_the_table(self):
        _, rows = self.kinds([typed("speed", 0, "TYPE_REAL")])
        parents = {row[0]: row[4] for row in rows}
        self.assertEqual(parents["hkChild"], "hkParent")
        self.assertEqual(parents["hkA"], "")

    def test_rejects_an_offset_the_two_files_disagree_on(self):
        layouts = {"hkA": entry(members=[("speed", 4)])}
        types = {"hkA": {"signature": "0x12345678", "members": [typed("speed", 0, "TYPE_REAL")]}}
        with self.assertRaisesRegex(ValueError, "in the layouts and"):
            hkx_layout_codegen.collect(layouts, types)


class RepositoryTests(unittest.TestCase):
    def test_committed_table_matches_the_radicle_owned_json(self):
        layouts = json.loads(LAYOUTS.read_text(encoding="utf-8"))["classes"]
        types = json.loads(TYPES.read_text(encoding="utf-8"))["classes"]
        rendered = hkx_layout_codegen.render(hkx_layout_codegen.collect(layouts, types))
        self.assertEqual(OUTPUT.read_text(encoding="utf-8"), rendered)
        self.assertNotIn("BGS/", rendered)


if __name__ == "__main__":
    unittest.main()
