import json
import os
import re
import tempfile
import subprocess
import unittest
from pathlib import Path
from unittest import mock

import native_ci_scope as scope

WORKFLOW = Path(__file__).resolve().parents[1] / "workflows" / "native-ci.yml"
if not WORKFLOW.exists():
    WORKFLOW = Path(__file__).resolve().parents[1] / "public-ci" / "workflows" / "native-ci.yml"


class NativeScopeTest(unittest.TestCase):
    def test_multiple_radicle_test_groups_are_selected_together(self):
        # Detects rebuilding every test binary when a PR only edits two test groups.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lists = root / "Radicle/cmake"
            lists.mkdir(parents=True)
            for group in ("core", "papyrus", "flash", "assets"):
                (lists / f"tests_{group}.cmake").write_text(
                    f"set(FO4_TESTS_{group.upper()}\n  tests/test_{group}.cpp\n)\n", encoding="utf-8")
            self.assertEqual(scope.test_targets(["Radicle/tests/test_core.cpp", "Radicle/tests/test_assets.cpp"], root),
                             ["fo4_tests_assets", "fo4_tests_core"])
            self.assertIsNone(scope.test_targets(["Radicle/tests/unmapped.cpp"], root))
            self.assertIsNone(scope.test_targets(["Radicle/src/core.cpp"], root))

    def test_real_data_service_only_does_not_build_native(self):
        # Detects rebuilding all native code for runner configuration changes.
        self.assertFalse(scope.needs_native(["ci/real-data-service/real_data_service.py",
                                             ".github/workflows/native-real-data.yml",
                                             "docs/code-notes/real-data-throttle.md"]))
        self.assertFalse(scope.needs_native(["PluginEditTool/web/src/MainShell.tsx", "Radicle/docs/PREVIS.md"]))

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
                self.assertEqual(output.read_text(encoding="utf-8"), f"native={expected}\ntargets=all\n")

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
                self.assertEqual(output.read_text(encoding="utf-8"), "native=true\ntargets=all\n")

    def run_scope(self, files, expected):
        head, base = "a" * 40, "b" * 40
        pr = {"head": {"sha": head}, "base": {"sha": base}, "changed_files": len(files)}
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            lists = root / "Radicle/cmake"
            lists.mkdir(parents=True)
            for group in ("core", "papyrus", "flash", "assets"):
                (lists / f"tests_{group}.cmake").write_text(
                    f"set(FO4_TESTS_{group.upper()}\n  tests/test_{group}.cpp\n)\n", encoding="utf-8")
            output = root / "output"
            output.write_text("", encoding="utf-8")
            results = [f"Source-Commit: {head}\nBase-Commit: {base}",
                       json.dumps(pr), json.dumps([files]), json.dumps(pr)]
            with mock.patch.object(scope, "command", side_effect=results), \
                    mock.patch.object(scope.os, "environ", dict(os.environ, GITHUB_REF_NAME="ci/pr-1",
                                                                 GITHUB_OUTPUT=str(output))):
                cwd = os.getcwd()
                os.chdir(root)
                try:
                    scope.main()
                finally:
                    os.chdir(cwd)
            self.assertEqual(output.read_text(encoding="utf-8"), f"native=true\ntargets={expected}\n")

    def test_test_only_pr_writes_narrowed_targets(self):
        self.run_scope([{"filename": "Radicle/tests/test_core.cpp"}], "fo4_tests_core")
        self.run_scope([{"filename": "Radicle/tests/test_core.cpp"},
                        {"filename": "Radicle/tests/test_assets.cpp"}],
                       "fo4_tests_assets fo4_tests_core")

    def test_native_ci_workflow_feeds_the_scope_targets_to_build_and_test(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("targets: ${{ steps.scope.outputs.targets }}", text)
        self.assertEqual(2, text.count("RADICLE_TEST_TARGETS: ${{ needs.scope.outputs.targets }}"))
        self.assertEqual(2, text.count("RADICLE_TEST_TARGET: ${{ needs.scope.outputs.targets }}"))
        self.assertIn('cmake --build build/native --target "${targets[@]}"', text)
        self.assertIn("cmake --build build\\native --target %RADICLE_TEST_TARGET%", text)

    def test_native_ci_workflow_label_pattern_selects_only_the_scoped_binaries(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn('labels="${RADICLE_TEST_TARGETS// /|}"', text)
        found = re.search(r'-L "([^"\n]*\$labels[^"\n]*)"', text)
        self.assertIsNotNone(found)
        selected = re.compile(found.group(1).replace("$labels", "fo4_tests_assets|fo4_tests_core"))
        self.assertIsNotNone(selected.fullmatch("fo4_tests_core"))
        self.assertIsNotNone(selected.fullmatch("fo4_tests_assets"))
        self.assertIsNone(selected.fullmatch("fo4_tests_flash"))
        self.assertIsNone(selected.fullmatch("fo4_tests_core fo4_tests_assets"))
        self.assertIn("('^(' + (($env:RADICLE_TEST_TARGET -split ' ') -join '|') + ')$')", text)


if __name__ == "__main__":
    unittest.main()
