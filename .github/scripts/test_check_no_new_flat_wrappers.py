import contextlib
import io
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import check_no_new_flat_wrappers as check


class UnexpectedTest(unittest.TestCase):
    def test_a_listed_name_passes(self):
        self.assertEqual([], check.unexpected(["c_api_armor.h"], {"c_api_armor.h"}))

    def test_a_new_name_is_reported(self):
        self.assertEqual(["c_api_weapon.h"], check.unexpected(["c_api_armor.h", "c_api_weapon.h"], {"c_api_armor.h"}))

    def test_a_removed_name_is_not_reported(self):
        self.assertEqual([], check.unexpected([], {"c_api_armor.h"}))


class ListedTest(unittest.TestCase):
    def test_finds_nested_matches_and_skips_build_folders(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "obj").mkdir()
            (root / "sub").mkdir()
            for name in ("A.cs", "obj/B.cs", "sub/C.cs", "D.txt"):
                (root / name).write_text("")
            self.assertEqual(["A.cs", "sub/C.cs"], sorted(check.listed(root, "*.cs", ("obj", "bin"))))


class MainTest(unittest.TestCase):
    def run_main(self, header, source):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "h").mkdir()
            (root / "s").mkdir()
            (root / "h" / header).write_text("")
            (root / "s" / source).write_text("")
            out = io.StringIO()
            with mock.patch.object(check, "HEADER_DIR", root / "h"), mock.patch.object(check, "CSHARP_DIR", root / "s"),                     contextlib.redirect_stdout(out):
                return check.main(), out.getvalue()

    def test_the_destructible_and_perk_exports_are_allowed(self):
        self.assertEqual((0, ""), self.run_main("c_api_perk.h", "PerkEffects.cs"))

    def test_an_unlisted_export_is_refused_with_its_name(self):
        code, text = self.run_main("c_api_weapon.h", "WeaponShim.cs")
        self.assertEqual(1, code)
        self.assertIn("c_api_weapon.h is a new file outside the flat-wrapper allow-list", text)
        self.assertIn("WeaponShim.cs is a new file outside the flat-wrapper allow-list", text)


class RepositoryTest(unittest.TestCase):
    def test_the_checked_in_tree_has_no_file_outside_the_allow_list(self):
        self.assertEqual([], check.unexpected(check.listed(check.HEADER_DIR, "c_api*.h"), check.HEADERS))
        self.assertEqual([], check.unexpected(check.listed(check.CSHARP_DIR, "*.cs", ("obj", "bin")), check.CSHARP))

    def test_every_allow_listed_file_still_exists(self):
        self.assertEqual([], sorted(check.HEADERS - set(check.listed(check.HEADER_DIR, "c_api*.h"))))
        self.assertEqual([], sorted(check.CSHARP - set(check.listed(check.CSHARP_DIR, "*.cs", ("obj", "bin")))))


if __name__ == "__main__":
    unittest.main()
