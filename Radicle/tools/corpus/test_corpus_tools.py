#!/usr/bin/env python3
"""Tests for the census, version and BA2 helpers of form_versions.py and ba2_measure.py."""

import contextlib
import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ba2_measure  # noqa: E402
import form_versions  # noqa: E402


def header(signature, size):
    return form_versions.HEADER.pack(signature, size, 0, 0, 0, 0, 0, 0)


def rec(signature, version):
    return form_versions.HEADER.pack(signature, 0, 0, 0x800, 0, 0, version, 0)


def grup(inner):
    return header(b"GRUP", 24 + len(inner)) + inner


def plugin(*parts):
    return rec(b"TES4", 131) + b"".join(parts)


def version_block(struct_version, major_minor, build):
    return form_versions.VS_SIGNATURE + struct.pack("<3I", struct_version, major_minor, build)


class FormVersions(unittest.TestCase):
    def test_census_counts_records_by_signature_and_version_and_groups(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "Mod.esp"
            path.write_bytes(plugin(grup(rec(b"MISC", 131) + rec(b"MISC", 120) + rec(b"WEAP", 131))))
            census = form_versions.read_plugin(path)
        self.assertEqual(census.histogram[("TES4", 131)], 1)
        self.assertEqual(census.histogram[("MISC", 120)], 1)
        self.assertEqual(census.histogram[("MISC", 131)], 1)
        self.assertEqual(census.groups, 1)
        misc = form_versions.summarize(census)["signatures"]["MISC"]
        self.assertEqual(misc["records"], 2)
        self.assertEqual(misc["min"], 120)
        self.assertEqual(misc["max"], 131)
        self.assertEqual(misc["below_current"], 1)

    def test_overruns_and_truncation_raise_value_error(self):
        cases = [
            ("missing TES4", b"XXXX" + bytes(20)),
            ("overruns", plugin(header(b"GRUP", 10))),
            ("overruns", plugin(header(b"GRUP", 24 + 1000))),
            ("truncated header", plugin(bytes(10))),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "Bad.esp"
            for expected, data in cases:
                path.write_bytes(data)
                with self.subTest(expected=expected):
                    with self.assertRaisesRegex(ValueError, expected):
                        form_versions.read_plugin(path)

    def test_game_version_reads_the_version_struct_and_skips_decoys(self):
        exe_bytes = (b"x" + form_versions.VS_SIGNATURE + b"decoy-decoy"
                     + version_block(form_versions.VS_STRUCT_VERSION, (1 << 16) | 11, 240 << 16))
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Fallout4.exe"
            self.assertIsNone(form_versions.game_version(exe))
            exe.write_bytes(exe_bytes)
            self.assertEqual(form_versions.game_version(exe), "1.11.240.0")
            exe.write_bytes(version_block(0x00020000, (1 << 16) | 11, 240 << 16))
            self.assertIsNone(form_versions.game_version(exe))

    def test_expand_orders_official_masters_then_listed_creations(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = root / "Data"
            data.mkdir()
            for name in ("Fallout4.esm", "DLCRobot.esm", "ccFoo.esl", "ccBar.esl"):
                (data / name).write_bytes(b"")
            (root / "Fallout4.ccc").write_text("ccFoo.esl\n", encoding="utf-8")
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                plugins, version = form_versions.expand([str(data)])
            self.assertEqual([p.name for p in plugins], ["Fallout4.esm", "DLCRobot.esm", "ccFoo.esl"])
            self.assertIsNone(version)
            self.assertIn("missing official master: DLCCoast.esm", err.getvalue())
            (root / "Fallout4.ccc").unlink()
            with contextlib.redirect_stderr(io.StringIO()):
                plugins, _ = form_versions.expand([str(data)])
            self.assertEqual([p.name for p in plugins], ["Fallout4.esm", "DLCRobot.esm", "ccBar.esl", "ccFoo.esl"])

    def test_main_json_exits_one_when_a_plugin_fails_and_keeps_the_good_ones(self):
        with tempfile.TemporaryDirectory() as tmp:
            good = Path(tmp) / "Good.esp"
            junk = Path(tmp) / "Junk.esp"
            good.write_bytes(plugin(rec(b"MISC", 131)))
            junk.write_bytes(b"not a plugin at all")
            out = io.StringIO()
            with mock.patch.object(sys, "argv", ["form_versions.py", "--json", str(good), str(junk)]):
                with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
                    status = form_versions.main()
        self.assertEqual(status, 1)
        self.assertEqual([p["name"] for p in json.loads(out.getvalue())["plugins"]], ["Good.esp"])


class Ba2Measure(unittest.TestCase):
    def test_exe_file_version_formats_four_parts_and_handles_missing_or_short_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Fallout4.exe"
            self.assertIsNone(ba2_measure.exe_file_version(exe))
            exe.write_bytes(version_block(form_versions.VS_STRUCT_VERSION, (1 << 16) | 11, 240 << 16))
            self.assertEqual(ba2_measure.exe_file_version(exe), "1.11.240.0")
            exe.write_bytes(b"no version resource here")
            self.assertIsNone(ba2_measure.exe_file_version(exe))
            exe.write_bytes(form_versions.VS_SIGNATURE + bytes(11))
            self.assertIsNone(ba2_measure.exe_file_version(exe))

    def test_exe_file_version_does_not_skip_decoys_unlike_form_versions(self):
        exe_bytes = (version_block(0x00020000, (1 << 16) | 2, (3 << 16) | 4)
                     + version_block(form_versions.VS_STRUCT_VERSION, (1 << 16) | 11, 240 << 16))
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "Fallout4.exe"
            exe.write_bytes(exe_bytes)
            self.assertEqual(form_versions.game_version(exe), "1.11.240.0")
            self.assertEqual(ba2_measure.exe_file_version(exe), "1.2.3.4")


if __name__ == "__main__":
    unittest.main()
