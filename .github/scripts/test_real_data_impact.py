import re
import io
from contextlib import redirect_stdout
import subprocess
import tempfile
import unittest
import os
from unittest import mock
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
    def test_pr_fast_tier_defers_whole_game_sweeps_but_keeps_focused_tests(self):
        # Detects reintroducing hours-long whole-game sweeps into the PR lane.
        excluded = impact.pr_slow_exclude()
        for name in (
            'OfficialMasters/TextExportReal.OfficialMasterRoundTripsToCanonicalBytes/"DLCCoast.esm"',
            'RowRoundtripReal.CellRowsSurviveWriteAndReread',
            'CApiLodReal.GeneratesCommonwealthLodEndToEnd',
            'HkxRepackReal.EveryVanillaHkxInTheArchivesRepacksToTheSameModelOrIsRefusedCleanly',
        ):
            self.assertRegex(name, excluded)
        for name in ('TextExportSampleReal.FalloutMasterSampleKeepsCompressedRecordsByteIdentical',
                     'AnimTextDataRealData.ShippedNamedListWithBlankEntryRoundTrips',
                     'HkxRepackReal.OneCommittedHkxRewritesToTheSameModel',
                     'WritebackRealData.OneDlcReferenceKeepsLinksWhenMastersMove',
                     'LipFileRealData.SampledShippedVoiceLipsDecodeToTheirDeclaredGrids',
                     'CheckErrorsReal.DlcSampleDiagnosticsAreDeterministic',
                     'LodObjectTileReal.FixedCommonwealthTilesMatchTheVanillaBtoBlockByBlockAndTriangleByTriangle',
                     'RowRoundtripReal.RegionRowsSurviveWriteAndReread'):
            self.assertNotRegex(name, excluded)

    def test_focused_native_replacements_are_in_the_same_suites_as_the_sweeps(self):
        # Detects a scoped suite selecting only a deferred test after its replacement is renamed or moved.
        root = Path(__file__).resolve().parents[2] / "Radicle" / "tests"
        for filename, suite, sample in (
            ("test_anim_text_data_real.cpp", "AnimTextDataRealData", "ShippedNamedListWithBlankEntryRoundTrips"),
            ("test_hkx_real.cpp", "HkxRepackReal", "OneCommittedHkxRewritesToTheSameModel"),
            ("test_writeback_real.cpp", "WritebackRealData", "OneDlcReferenceKeepsLinksWhenMastersMove"),
            ("test_lip_file_real.cpp", "LipFileRealData", "SampledShippedVoiceLipsDecodeToTheirDeclaredGrids"),
            ("test_check_errors_real.cpp", "CheckErrorsReal", "DlcSampleDiagnosticsAreDeterministic"),
        ):
            code = (root / filename).read_text(encoding="utf-8")
            names = {f"{match.group(1)}.{match.group(2)}" for match in impact.scan.GTEST_HEAD.finditer(code)}
            self.assertIn(f"{suite}.{sample}", names)
            self.assertFalse(re.search(impact.pr_slow_exclude(), f"{suite}.{sample}"))

    def test_pr_lane_writes_fast_exclusions_but_full_run_does_not(self):
        # Detects applying the slow-tier exclusion to the full daily run or not applying it to PRs.
        env_file = self.root / "env.txt"
        changed = self.root / "changed.txt"
        changed.write_text("Radicle/CMakeLists.txt", encoding="utf-8")
        with mock.patch.object(impact, "suite_digests", return_value=None), \
                mock.patch.object(os, "environ", dict(os.environ, GITHUB_EVENT_NAME="pull_request", REAL_DATA_FULL="")):
            impact.main(["--changed-file", str(changed), "--github-env", str(env_file)])
            self.assertIn("CApiLodReal", env_file.read_text(encoding="utf-8"))
        env_file.unlink()
        with mock.patch.object(impact, "suite_digests", return_value=None), \
                mock.patch.object(os, "environ", dict(os.environ, GITHUB_EVENT_NAME="pull_request", REAL_DATA_FULL="1")):
            impact.main(["--changed-file", str(changed), "--github-env", str(env_file)])
        self.assertFalse(env_file.exists())

    def test_pr_whole_world_lod_change_selects_the_fixed_tile_test(self):
        # Detects a PR selecting only the deferred whole-world LOD test and running nothing useful.
        suites = {"CApiLodReal": set(), "LodObjectTileReal": set(), "OtherReal": set()}
        with mock.patch.object(impact, "affected_suites", return_value=({"CApiLodReal"}, set(suites))):
            _, exclude = impact.decide(["lod.cpp"], suites, {}, proven={"LodObjectTileReal"}, fast_pr=True)
        self.assertFalse(re.search(exclude, "LodObjectTileReal.FixedCommonwealthTilesMatch"))
        self.assertTrue(re.search(exclude, "OtherReal.Reads"))

    def test_pr_text_export_sweep_selects_the_real_master_sample_even_if_cached(self):
        # Detects a PR selecting only deferred DLC sweeps when the sample has a prior proof.
        suites = {"TextExportReal": set(), "TextExportSampleReal": set(), "OtherReal": set()}
        with mock.patch.object(impact, "affected_suites", return_value=({"TextExportReal"}, set(suites))):
            _, exclude = impact.decide(["text_export.cpp"], suites, {}, proven={"TextExportSampleReal"}, fast_pr=True)
        self.assertFalse(re.search(exclude, "TextExportSampleReal.FalloutMasterSampleKeepsCompressedRecordsByteIdentical"))
        self.assertTrue(re.search(exclude, "OtherReal.Reads"))

    def test_pr_master_diagnostics_select_the_bounded_real_sample(self):
        # Detects a scoped diagnostic change with only the deferred master sweep selected.
        suites = {"CheckErrors": set(), "CheckErrorsReal": set(), "OtherReal": set()}
        with mock.patch.object(impact, "affected_suites", return_value=({"CheckErrors"}, set(suites))):
            _, exclude = impact.decide(["test_check_errors.cpp"], suites, {}, proven={"CheckErrorsReal"}, fast_pr=True)
        self.assertFalse(re.search(exclude, "CheckErrorsReal.DlcSampleDiagnosticsAreDeterministic"))

    def test_selector_log_reports_cached_focused_substitute_as_running(self):
        # Detects a cached sample reported as skipped even though it is selected as a substitute.
        suites = {"TextExportReal": set(), "TextExportSampleReal": set(), "OtherReal": set()}
        changed = self.root / "changed.txt"
        changed.write_text("Radicle/src/a/x.cpp", encoding="utf-8")
        output = io.StringIO()
        with mock.patch.object(impact, "real_data_suites", return_value=suites), \
                mock.patch.object(impact, "build_deps", return_value={}), \
                mock.patch.object(impact, "suite_digests", return_value={"TextExportSampleReal": "digest"}), \
                mock.patch.object(impact, "read_json", return_value={"TextExportSampleReal": "digest"}), \
                mock.patch.object(impact, "affected_suites", return_value=({"TextExportReal"}, set(suites))), \
                mock.patch.object(impact, "test_source_targets", return_value=None), \
                mock.patch.object(os, "environ", dict(os.environ, GITHUB_EVENT_NAME="pull_request", REAL_DATA_FULL="",
                                                    GITHUB_ENV="", REAL_DATA_HASH_FILE="")), \
                redirect_stdout(output):
            impact.main(["--changed-file", str(changed), "--list"])
        self.assertIn("RUN  TextExportSampleReal", output.getvalue())

    def test_module_dependency_edges_are_reused_across_test_files(self):
        # Detects rescanning the same module for every suite in a scoped PR.
        includes = impact.Includes()
        start = {impact.module((self.root / "Radicle/src/a/x.cpp").resolve())}
        with mock.patch.object(includes, "of", wraps=includes.of) as read:
            first = impact.module_closure(start, includes)
            calls = read.call_count
            self.assertGreater(calls, 0)
            self.assertEqual(impact.module_closure(start, includes), first)
            self.assertEqual(read.call_count, calls)

    def test_only_scoped_runs_without_pex_consumers_skip_staging(self):
        # Detects extracting the entire vanilla PEX corpus for an unrelated PR,
        # and detects skipping it when either consumer is selected.
        excluded = impact.exclude_regex({"PapyrusCodegenReal", "PapyrusDecompilerReal"})
        self.assertFalse(impact.needs_pex_stage(excluded))
        for suites in (set(), {"PapyrusCodegenReal"}, {"PapyrusDecompilerReal"}):
            self.assertTrue(impact.needs_pex_stage(impact.exclude_regex(suites)))

    def test_pex_staging_consumers_cover_real_test_sources(self):
        # Detects a new PEX corpus consumer being skipped by the staging decision.
        tests = Path(__file__).resolve().parents[2] / "Radicle" / "tests"
        consumers = set()
        for path in tests.rglob("*.cpp"):
            code = path.read_text(encoding="utf-8", errors="replace")
            if '"FO4RE_PEX_CORPUS"' in code:
                consumers.update(match.group(1) for match in impact.scan.GTEST_HEAD.finditer(code))
        self.assertTrue(consumers)
        self.assertLessEqual(consumers, impact.PEX_STAGE_SUITES)

    def test_unrelated_pr_writes_the_skip_staging_control(self):
        # Detects computing the scoped decision without passing it to the workflow.
        env_file = self.root / "env.txt"
        changed = self.root / "changed.txt"
        changed.write_text("Radicle/tests/test_x_real.cpp", encoding="utf-8")
        suites = {name: set() for name in ("XReal", *impact.PEX_STAGE_SUITES)}
        with mock.patch.object(impact, "real_data_suites", return_value=suites), \
                mock.patch.object(impact, "build_deps", return_value={}), \
                mock.patch.object(impact, "suite_digests", return_value=None), \
                mock.patch.object(impact, "affected_suites", return_value=({"XReal"}, set(suites))), \
                mock.patch.object(os, "environ", dict(os.environ, GITHUB_EVENT_NAME="pull_request", REAL_DATA_FULL="")):
            impact.main(["--changed-file", str(changed), "--github-env", str(env_file)])
        self.assertIn("REAL_DATA_STAGE_PEX=0\n", env_file.read_text(encoding="utf-8"))

    def test_build_targets_cover_every_source_of_each_selected_suite(self):
        # Detects leaving one binary stale when a suite is present in multiple executables.
        a, b, c = (self.root / name for name in ("a.cpp", "b.cpp", "c.cpp"))
        suites = {"SelectedReal": {a, b}, "OtherReal": {c}}
        source_targets = {a: {"fo4_tests_core"}, b: {"fo4_tests_assets"}, c: {"fo4_tests_flash"}}
        exclude = impact.exclude_regex({"OtherReal"})
        self.assertEqual(impact.scoped_build_targets(suites, exclude, source_targets),
                         {"fo4_tests_core", "fo4_tests_assets"})
        self.assertIsNone(impact.scoped_build_targets(suites, exclude, {a: {"fo4_tests_core"}}))

    def test_real_cmake_lists_map_sources_to_their_binaries(self):
        # Detects a mapping that misses a test binary after source lists change.
        root = Path(__file__).resolve().parents[2]
        with mock.patch.object(impact, "ROOT", root):
            targets = impact.test_source_targets()
        self.assertIsNotNone(targets)
        self.assertEqual(targets[(root / "Radicle/tests/test_row_roundtrip_real.cpp").resolve()], {"fo4_tests_core"})
        self.assertEqual(targets[(root / "Radicle/tests/test_c_api_lod_real.cpp").resolve()], {"fo4_tests_assets"})

    def test_unrecognized_cmake_lists_do_not_narrow_the_build(self):
        # Detects dropping tests when a future CMake source list cannot be parsed safely.
        self.assertIsNone(impact.test_source_targets())

    def test_scoped_target_selection_reaches_the_workflow_environment(self):
        # Detects a correct target plan being lost before the Windows build step.
        source = (self.root / "Radicle/tests/test_x_real.cpp").resolve()
        suites = {"XReal": {source}, "OtherReal": {self.root / "other.cpp"}}
        changed = self.root / "changed.txt"
        changed.write_text("Radicle/tests/test_x_real.cpp", encoding="utf-8")
        env_file = self.root / "env.txt"
        with mock.patch.object(impact, "real_data_suites", return_value=suites), \
                mock.patch.object(impact, "build_deps", return_value={}), \
                mock.patch.object(impact, "suite_digests", return_value=None), \
                mock.patch.object(impact, "affected_suites", return_value=({"XReal"}, set(suites))), \
                mock.patch.object(impact, "test_source_targets", return_value={source: {"fo4_tests_core"}}), \
                mock.patch.object(os, "environ", dict(os.environ, GITHUB_EVENT_NAME="pull_request", REAL_DATA_FULL="")):
            impact.main(["--changed-file", str(changed), "--github-env", str(env_file)])
        self.assertIn("REAL_DATA_BUILD_TARGETS=fo4_tests_core\n", env_file.read_text(encoding="utf-8"))

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

    def test_unavailable_proof_skips_git_inventory(self):
        # Detects wasting time collecting every tracked blob when no proof can be made.
        with mock.patch.object(impact, "tracked_blobs", side_effect=AssertionError("unneeded git scan")):
            self.assertIsNone(impact.suite_digests({}, {}, "", "context"))
            self.assertIsNone(impact.suite_digests({}, {}, "game", ""))

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
