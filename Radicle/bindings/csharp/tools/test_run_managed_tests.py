#!/usr/bin/env python3
"""Tests for the CTest entry lookup and the parent build file guard of run_managed_tests.py."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_managed_tests  # noqa: E402


class RunManagedTestsTest(unittest.TestCase):
    def run_main(self, listing):
        with mock.patch.object(sys, 'argv', ['run_managed_tests.py', '--build-dir', 'build']):
            with mock.patch.object(run_managed_tests.subprocess, 'check_output', return_value=listing):
                run_managed_tests.main()

    def test_missing_csharp_entry_names_cmake_option(self):
        with self.assertRaises(SystemExit) as caught:
            self.run_main(json.dumps({'tests': [{'name': 'other'}]}))
        self.assertIn('-DRADICLE_CSHARP_TESTS=ON', str(caught.exception))

    def test_entry_without_environment_names_cmake_option(self):
        listing = json.dumps({'tests': [{'name': 'radicle_csharp_bindings', 'properties': []}]})
        with self.assertRaises(SystemExit) as caught:
            self.run_main(listing)
        self.assertIn('-DRADICLE_CSHARP_TESTS=ON', str(caught.exception))

    def test_parent_build_files_stop_at_repo_root(self):
        with tempfile.TemporaryDirectory() as temporary:
            outer = Path(temporary)
            repo = outer / 'repo'
            bindings = repo / 'Radicle' / 'bindings' / 'csharp'
            bindings.mkdir(parents=True)
            (repo / '.git').mkdir()
            (repo / 'Directory.Build.props').write_text('<Project />', encoding='utf-8')
            (repo / 'Radicle' / 'NuGet.Config').write_text('<configuration />', encoding='utf-8')
            (outer / 'Directory.Build.props').write_text('<Project />', encoding='utf-8')
            found = run_managed_tests.parent_build_files(bindings)
        self.assertEqual(sorted(found), sorted([repo / 'Directory.Build.props', repo / 'Radicle' / 'NuGet.Config']))


if __name__ == '__main__':
    unittest.main()
