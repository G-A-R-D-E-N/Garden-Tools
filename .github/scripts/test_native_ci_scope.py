import json
import os
import tempfile
import subprocess
import unittest
from pathlib import Path
from unittest import mock

import native_ci_scope as scope


class NativeScopeTest(unittest.TestCase):
    def test_real_data_service_only_does_not_build_native(self):
        # Detects rebuilding all native code for runner configuration changes.
        self.assertFalse(scope.needs_native(["ci/real-data-service/real_data_service.py",
                                             ".github/workflows/native-real-data.yml",
                                             "docs/code-notes/real-data-throttle.md"]))

    def test_native_build_inputs_and_unknown_paths_stay_conservative(self):
        # Detects skipping a native build when code, CMake, or its workflow changes.
        for path in ("Radicle/src/foo.cpp", "PluginEditTool/native/CMakeLists.txt",
                     ".github/public-ci/workflows/native-ci.yml", "unknown/new.file"):
            with self.subTest(path=path):
                self.assertTrue(scope.needs_native([path]))
        self.assertTrue(scope.needs_native([]))

    def test_renamed_native_file_is_not_treated_as_ci_only(self):
        # Detects hiding native changes by moving the old path into docs.
        entries = [{"filename": "docs/old.cpp", "previous_filename": "Radicle/src/old.cpp"}]
        self.assertTrue(scope.needs_native(scope.file_paths(entries)))

    def test_private_pr_metadata_must_match_exported_head_and_base(self):
        # Detects a stale export, incomplete files list or API failure passing without native CI.
        head, base = "a" * 40, "b" * 40
        pr = {"head": {"sha": head}, "base": {"sha": base}, "changed_files": 1}
        files = [[{"filename": "ci/real-data-service/real_data_service.py"}]]
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "output"
            for exported_base, entries, expected in ((base, files, "false"), ("c" * 40, files, "true"),
                                                       (base, [[]], "true")):
                output.write_text("", encoding="utf-8")
                results = [f"Source-Commit: {head}\nBase-Commit: {exported_base}",
                           json.dumps(pr), json.dumps(entries), json.dumps(pr)]
                with mock.patch.object(scope, "command", side_effect=results), \
                        mock.patch.object(scope.os, "environ", dict(os.environ, GITHUB_REF_NAME="ci/pr-1",
                                                                     GITHUB_OUTPUT=str(output))):
                    scope.main()
                self.assertEqual(output.read_text(encoding="utf-8"), f"native={expected}\n")

    def test_api_failure_and_non_pr_branches_run_native(self):
        # Detects an API outage or main-branch export accidentally skipping builds.
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "output"
            for branch in ("ci/pr-1", "ci/main"):
                output.write_text("", encoding="utf-8")
                with mock.patch.object(scope, "command", side_effect=subprocess.CalledProcessError(1, "gh")), \
                        mock.patch.object(scope.os, "environ", dict(os.environ, GITHUB_REF_NAME=branch,
                                                                     GITHUB_OUTPUT=str(output))):
                    scope.main()
                self.assertEqual(output.read_text(encoding="utf-8"), "native=true\n")


if __name__ == "__main__":
    unittest.main()
