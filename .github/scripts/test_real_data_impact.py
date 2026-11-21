import re
import subprocess
import tempfile
import unittest
from pathlib import Path

import real_data_impact as impact

FILES = {
    "Radicle/include/fo4/a/x.hpp": "",
    "Radicle/src/a/x.cpp": '#include "fo4/a/x.hpp"\n#include <fo4/b/y.hpp>\n',
    "Radicle/include/fo4/b/y.hpp": "",
    "Radicle/src/b/y.cpp": '#include "fo4/b/y.hpp"\n#include "fo4/c/z.hpp"\n',
    "Radicle/include/fo4/c/z.hpp": "",
    "Radicle/src/c/z.cpp": '#include "fo4/c/z.hpp"\n#include "fo4/a/x.hpp"\n',
    "Radicle/src/c/alternate.cpp": "",
    "Radicle/src/b/alternate.cpp": "",
    "Radicle/src/c/orphan.cpp": "",
    "Radicle/src/c_api_runtime.cpp": "",
    "Radicle/include/fo4/w/w.hpp": "",
    "Radicle/src/w/w.cpp": '#include "fo4/w/w.hpp"\n',
    "Radicle/include/fo4/e/widget.hpp": "",
    "Radicle/src/e/widget.cpp": '#include "fo4/e/widget.hpp"\n',
    "Radicle/src/e/hidden.cpp": '#include "fo4/e/dependency.inc"\n',
    "Radicle/include/fo4/e/dependency.inc": '#include <fo4/c/z.hpp>\n',
    "Radicle/tests/test_x_real.cpp": '#include "fo4/a/x.hpp"\nTEST(XReal, Reads) {}\n',
    "Radicle/tests/test_z_real.cpp": '#include "fo4/c/z.hpp"\nTEST(ZReal, Reads) {}\nTEST(ZPlain, Reads) {}\n',
    "Radicle/tests/test_w_real.cpp": '#include "fo4/w/w.hpp"\nTEST(WReal, Reads) {}\n',
    "Radicle/tests/test_e_real.cpp": '#include "fo4/e/widget.hpp"\nTEST(EReal, Reads) {}\n',
    "PluginEditTool/native/include/fo4ide/a/widget.hpp": "",
    "PluginEditTool/native/src/a/widget.cpp": '#include <fo4ide/a/widget.hpp>\n#include <fo4ide/b/api.hpp>\n',
    "PluginEditTool/native/include/fo4ide/b/api.hpp": "",
    "PluginEditTool/native/src/b/api.cpp": '#include <fo4ide/b/api.hpp>\n',
    "PluginEditTool/native/src/b/alternate.cpp": "",
    "PluginEditTool/native/tests/test_native_real.cpp": '#include <fo4ide/a/widget.hpp>\nTEST(NativeReal, Reads) {}\n',
    "Radicle/tests/test_text_export_real.cpp":
        "TEST_P(TextExportReal, OfficialMasterRoundTripsToCanonicalBytes) {}\n"
        "TEST(TextExportSampleReal, FalloutMasterSampleKeepsCompressedRecordsByteIdentical) {}\n",
    ".github/workflows/native-real-data.yml": "env:\n  REAL_DATA_TEST_REGEX: 'Real\\.'\n",
}


class ImpactTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name).resolve()
        for name, text in FILES.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
        self.saved = (impact.ROOT, impact.INCLUDE_ROOTS, impact.SOURCE_TREES, impact.WORKFLOW, impact.scan.TEST_FILES)
        impact.ROOT = self.root
        impact.INCLUDE_ROOTS = (self.root / "Radicle/include", self.root / "Radicle/src", self.root / "Radicle/tests",
                                self.root / "PluginEditTool/native/include", self.root / "PluginEditTool/native/src",
                                self.root / "PluginEditTool/native/tests")
        impact.SOURCE_TREES = ((self.root / "Radicle/include/fo4", self.root / "Radicle/src"),
                               (self.root / "PluginEditTool/native/include/fo4ide",
                                self.root / "PluginEditTool/native/src"))
        impact.WORKFLOW = self.root / ".github/workflows/native-real-data.yml"
        impact.scan.TEST_FILES = sorted(
            path for root in (self.root / "Radicle/tests", self.root / "PluginEditTool/native/tests")
            for path in root.rglob("*") if path.suffix in {".cpp", ".hpp", ".h"})

    def tearDown(self):
        (impact.ROOT, impact.INCLUDE_ROOTS, impact.SOURCE_TREES, impact.WORKFLOW, impact.scan.TEST_FILES) = self.saved
        self.tmp.cleanup()

    def decide(self, *changed, proven=frozenset()):
        suites = impact.real_data_suites()
        return impact.decide(list(changed), suites, impact.build_deps(suites), proven)

    def test_only_selected_suites_are_known(self):
        self.assertEqual(set(impact.real_data_suites()), {
            "XReal", "ZReal", "WReal", "EReal", "NativeReal", "TextExportReal", "TextExportSampleReal"})

    def test_public_headers_and_source_files_share_module_ids(self):
        self.assertEqual(
            impact.module((self.root / "Radicle/include/fo4/a/x.hpp").resolve()),
            impact.module((self.root / "Radicle/src/a/x.cpp").resolve()))
        self.assertEqual(
            impact.module((self.root / "PluginEditTool/native/include/fo4ide/a/widget.hpp").resolve()),
            impact.module((self.root / "PluginEditTool/native/src/a/widget.cpp").resolve()))

    def test_a_header_two_levels_down_reaches_the_test_that_includes_its_user(self):
        mode, exclude = self.decide("Radicle/src/b/y.cpp")
        self.assertEqual(mode, "some")
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertTrue(re.search(exclude, "WReal.Reads"))

    def test_module_dependencies_reach_unmatched_implementations_through_cycles(self):
        _, exclude = self.decide("Radicle/src/b/alternate.cpp")
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        self.assertFalse(re.search(exclude, "EReal.Reads"))
        self.assertTrue(re.search(exclude, "WReal.Reads"))

    def test_cross_module_dependency_reaches_a_different_implementation_file(self):
        _, exclude = self.decide("Radicle/src/c/alternate.cpp")
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        self.assertFalse(re.search(exclude, "EReal.Reads"))
        self.assertTrue(re.search(exclude, "WReal.Reads"))

    def test_native_include_and_source_paths_share_a_module_identity(self):
        _, exclude = self.decide("PluginEditTool/native/src/b/alternate.cpp")
        self.assertFalse(re.search(exclude, "NativeReal.Reads"))
        self.assertTrue(re.search(exclude, "XReal.Reads"))

    def test_macro_and_unresolved_local_includes_fail_closed(self):
        source = self.root / "Radicle/src/a/x.cpp"
        for include in ('#include HEADER_MACRO\n', '#include "missing_local.hpp"\n',
                        '#include <missing_project.hpp>\n', '#include <missing_project.h>\n'):
            source.write_text(include, encoding="utf-8")
            self.assertEqual(self.decide("Radicle/src/a/x.cpp"), ("all", ""))

    def test_ambiguous_include_fails_closed(self):
        roots = [self.root / "include-one", self.root / "include-two"]
        for root in roots:
            (root / "ambiguous.hpp").parent.mkdir(parents=True, exist_ok=True)
            (root / "ambiguous.hpp").write_text("", encoding="utf-8")
        (self.root / "Radicle/src/a/ambiguous.cpp").write_text(
            "#include <ambiguous.hpp>\n", encoding="utf-8")
        impact.INCLUDE_ROOTS += tuple(roots)
        self.assertEqual(self.decide("Radicle/src/a/x.cpp"), ("all", ""))

    def test_root_level_generated_and_unknown_non_test_changes_fail_closed(self):
        for path in ("Radicle/src/c_api_runtime.cpp", "Radicle/src/a/generated.hpp",
                     "Radicle/src/a/unknown.c", "Radicle/include/fo4/e/dependency.inc"):
            self.assertEqual(self.decide(path), ("all", ""), path)

    def test_real_skin_implementation_change_reaches_nif_corpus(self):
        (impact.ROOT, impact.INCLUDE_ROOTS, impact.SOURCE_TREES,
         impact.WORKFLOW, impact.scan.TEST_FILES) = self.saved
        if not impact.WORKFLOW.is_file():
            impact.WORKFLOW = impact.scan.WORKFLOW
        suites = impact.real_data_suites()
        result = impact.affected_suites(["Radicle/src/nif/skin.cpp"], suites, impact.build_deps(suites))
        self.assertIsNotNone(result)
        self.assertIn("NifCorpus", result[0])

    def test_module_closure_selects_transitive_cross_module_dependants(self):
        mode, exclude = self.decide("Radicle/include/fo4/c/z.hpp")
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        self.assertTrue(re.search(exclude, "WReal.Reads"))

    def test_exclusion_matches_a_parameterized_test_name(self):
        exclude = impact.exclude_regex({"TextExportReal", "TextExportSampleReal"})
        self.assertTrue(re.search(exclude,
                                  "OfficialMasters/TextExportReal.OfficialMasterRoundTripsToCanonicalBytes/0"))
        self.assertTrue(re.search(exclude,
                                  "TextExportSampleReal.FalloutMasterSampleKeepsCompressedRecordsByteIdentical"))

    def test_a_source_file_without_a_header_selects_its_module(self):
        _, exclude = self.decide("Radicle/src/c/orphan.cpp")
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        self.assertTrue(re.search(exclude, "WReal.Reads"))

    def test_a_changed_test_file_runs_its_own_suite(self):
        _, exclude = self.decide("Radicle/tests/test_x_real.cpp")
        self.assertFalse(re.search(exclude, "XReal.Reads"))
        self.assertTrue(re.search(exclude, "ZReal.Reads"))

    def test_docs_only_runs_nothing(self):
        self.assertEqual(self.decide("docs/code-notes/x.md", "Radicle/docs/y.md")[0], "none")

    def test_docs_only_does_not_need_a_complete_source_graph(self):
        (self.root / "Radicle/src/a/x.cpp").write_text("#include UNKNOWN_HEADER\n", encoding="utf-8")
        self.assertEqual(self.decide("docs/code-notes/x.md"), ("none", ""))

    def test_build_files_and_unknown_file_types_run_everything(self):
        for path in ("Radicle/CMakeLists.txt", "cmake/x.cmake", "Radicle/schema/fields.json", "Radicle/src/a/data.bin",
                     ".github/scripts/real_data_impact.py", ".github/workflows/native-real-data.yml"):
            self.assertEqual(self.decide(path), ("all", ""), path)

    def test_a_deleted_header_runs_everything_because_no_test_includes_it_any_more(self):
        self.assertEqual(self.decide("Radicle/include/fo4/gone/old.hpp"), ("all", ""))

    def test_exclusion_avoids_the_group_alternation_that_ctest_ignores(self):
        _, exclude = self.decide("Radicle/include/fo4/c/z.hpp")
        self.assertNotIn("(^|", exclude)
        self.assertNotIn("|/", exclude)

    def test_an_unknown_diff_runs_everything(self):
        suites = impact.real_data_suites()
        self.assertEqual(impact.decide(None, suites, impact.build_deps(suites)), ("all", ""))

    def stage_all(self):
        for args in (["init", "-q"], ["add", "-A"]):
            subprocess.run(["git", *args], cwd=self.root, check=True, capture_output=True)

    def digests(self, game="g1", context="c1"):
        suites = impact.real_data_suites()
        return impact.suite_digests(suites, impact.build_deps(suites), game, context)

    def test_a_proof_needs_the_game_build(self):
        self.stage_all()
        self.assertIsNone(self.digests(game=""))

    def test_a_proof_needs_git_to_know_every_file(self):
        self.assertIsNone(self.digests())

    def test_a_digest_moves_only_with_the_files_a_suite_runs(self):
        self.stage_all()
        before = self.digests()
        (self.root / "Radicle/src/b/y.cpp").write_text('#include "fo4/b/y.hpp"\n// edit\n', encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)
        after = self.digests()
        self.assertEqual(before["WReal"], after["WReal"])
        self.assertNotEqual(before["ZReal"], after["ZReal"])
        self.assertNotEqual(before["XReal"], after["XReal"])

    def test_digest_covers_other_implementations_in_a_reached_module(self):
        self.stage_all()
        before = self.digests()
        (self.root / "Radicle/src/b/alternate.cpp").write_text("int changed;\n", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)
        after = self.digests()
        self.assertNotEqual(before["XReal"], after["XReal"])
        self.assertNotEqual(before["ZReal"], after["ZReal"])
        self.assertEqual(before["WReal"], after["WReal"])

    def test_uncertain_dependencies_disable_proof_reuse(self):
        self.stage_all()
        source = self.root / "Radicle/src/a/x.cpp"
        source.write_text("#include UNKNOWN_HEADER\n", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)
        self.assertIsNone(self.digests())
        self.assertEqual(self.decide("Radicle/src/a/x.cpp", proven=set()), ("all", ""))

    def test_a_digest_moves_with_the_game_build_and_with_shared_inputs(self):
        self.stage_all()
        base = self.digests()
        self.assertNotEqual(base["ZReal"], self.digests(game="g2")["ZReal"])
        self.assertNotEqual(base["ZReal"], self.digests(context="c2")["ZReal"])
        self.assertIsNone(self.digests(context=""))
        (self.root / "Radicle/schema").mkdir()
        (self.root / "Radicle/schema/fields.json").write_text("{}", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)
        self.assertNotEqual(base["ZReal"], self.digests()["ZReal"])

    def test_a_proven_suite_is_excluded_and_all_proven_runs_nothing(self):
        mode, exclude = self.decide("Radicle/include/fo4/b/y.hpp", proven={"XReal"})
        self.assertEqual(mode, "some")
        self.assertTrue(re.search(exclude, "XReal.Reads"))
        self.assertFalse(re.search(exclude, "ZReal.Reads"))
        suites = impact.real_data_suites()
        mode, exclude = impact.decide(None, suites, impact.build_deps(suites), {"ZReal"})
        self.assertEqual(mode, "some")
        self.assertTrue(re.search(exclude, "ZReal.Reads"))
        self.assertFalse(re.search(exclude, "XReal.Reads"))


if __name__ == "__main__":
    unittest.main()
