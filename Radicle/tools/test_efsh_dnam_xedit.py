#!/usr/bin/env python3
"""Checks the EFSH DNAM scalar rows in fields.json against the pinned xEdit Data struct (form version 106 and later)."""

import json
import re
import unittest
from pathlib import Path

XEDIT = Path.home() / "ds-proxy" / "xedit-src" / "Core" / "wbDefinitionsFO4.pas"
FIELDS = Path(__file__).resolve().parent.parent / "schema" / "fallout4" / "fields.json"
WIDTH = {"itU8": 1, "itU16": 2, "itU32": 4, "itS32": 4}
MEMBER = re.compile(r"^\s*wb(Float|ByteColors|FormIDCk|Integer)\('")


def xedit_members():
    lines = XEDIT.read_text(encoding="utf-8", errors="replace").splitlines()
    start = next(i for i, line in enumerate(lines) if line.strip() == "wbRecord(EFSH, 'Effect Shader', [")
    begin = next(i for i in range(start, len(lines)) if lines[i].strip() == "wbStruct('Data', [")
    members, depth, offset = [], 0, 0
    for line in lines[begin:]:
        text = line.strip()
        if text.startswith("wbStruct("):
            depth += 1
            continue
        if text.startswith("])") and not text.startswith("]))") and depth:
            depth -= 1
            if depth == 0:
                break
            continue
        match = MEMBER.match(line)
        if not match:
            continue
        kind = match.group(1)
        width = 4
        if kind == "Integer":
            width = WIDTH[re.search(r"\b(it[SU]\d+)\b", line).group(1)]
        if kind == "ByteColors":
            members.extend((offset + channel, 1, "Integer") for channel in range(3))
        elif kind != "FormIDCk":
            members.append((offset, width, kind))
        offset += width
    return members, offset


def schema_rows():
    rows = []

    def walk(node):
        if isinstance(node, dict):
            if str(node.get("path", "")).startswith("EFSH\\") and node.get("subrecord") == "DNAM":
                rows.append(node)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)

    walk(json.loads(FIELDS.read_text(encoding="utf-8")))
    return rows


@unittest.skipUnless(XEDIT.exists(), "pinned wbDefinitionsFO4.pas is not in ~/ds-proxy/xedit-src/Core")
class EfshDnamXeditTests(unittest.TestCase):
    def test_every_dnam_row_sits_at_an_xedit_member_offset_with_its_width(self):
        members, total = xedit_members()
        self.assertEqual(total, 157)
        self.assertNotIn(108, {offset for offset, _, _ in members})
        by_offset = {offset: (width, kind) for offset, width, kind in members}
        rows = schema_rows()
        self.assertEqual(len(rows), len(members))
        self.assertEqual({row["offset"] for row in rows}, set(by_offset))
        for row in rows:
            with self.subTest(path=row["path"]):
                width, kind = by_offset[row["offset"]]
                self.assertEqual(row["min_form_version"], 106)
                if kind == "Float":
                    self.assertEqual(row["storage"], "f32")
                else:
                    self.assertEqual(row["storage"], {1: "u8", 2: "u16", 4: "u32"}[width])


if __name__ == "__main__":
    unittest.main()
