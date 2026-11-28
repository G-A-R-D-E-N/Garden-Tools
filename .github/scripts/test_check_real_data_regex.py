import contextlib
import io
import re
import tempfile
import unittest
from pathlib import Path

import check_real_data_regex as check

HEADER = """#pragma once
#include <cstdlib>
inline const char* data_root() { return std::getenv("FO4_DATA_DIR"); }
inline const char* nested_root() { return data_root(); }
inline int plain() { return 1; }
"""

SOURCE = """#include "helpers.hpp"
const auto data_dir = [] { return std::getenv("FO4_DATA_DIR"); };
const auto load = [&](int x) -> const char* { return x ? data_dir() : nullptr; };
class CtorFixture : public testing::Test {
 public:
  CtorFixture() : dir_(std::getenv("FO4_DATA_DIR")) {}
  const char* dir_;
};
class SuiteFixture : public testing::Test {
 public:
  static inline const char* dir = nullptr;
  static void SetUpTestSuite() { dir = std::getenv("FO4_DATA_DIR"); }
};
TEST(HeaderHelper, Reads) { EXPECT_NE(data_root(), nullptr); }
TEST(NestedHelper, Reads) { EXPECT_NE(nested_root(), nullptr); }
TEST(LambdaHelper, Reads) { EXPECT_NE(load(1), nullptr); }
TEST_F(CtorFixture, Reads) { EXPECT_NE(dir_, nullptr); }
TEST_F(SuiteFixture, Reads) { EXPECT_NE(SuiteFixture::dir, nullptr); }
TEST(Plain, NoData) { EXPECT_EQ(plain(), 1); }
"""


class RealDataTestsTest(unittest.TestCase):
    def test_selects_every_data_access_shape_and_skips_plain_tests(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            header = root / "helpers.hpp"
            source = root / "cases.cpp"
            header.write_text(HEADER, encoding="utf-8")
            source.write_text(SOURCE, encoding="utf-8")
            found = {test_id for _, _, test_id in check.real_data_tests([header, source])}
        self.assertEqual(found, {"HeaderHelper.Reads", "NestedHelper.Reads", "LambdaHelper.Reads",
                                 "CtorFixture.Reads", "SuiteFixture.Reads"})

    def temp_file(self, name, text):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = Path(tmp.name) / name
        path.write_text(text, encoding="utf-8")
        return path

    def run_main(self, path, regex):
        self.addCleanup(setattr, check, "TEST_FILES", check.TEST_FILES)
        self.addCleanup(setattr, check, "ROOT", check.ROOT)
        self.addCleanup(setattr, check, "configured_regex", check.configured_regex)
        check.TEST_FILES = [path]
        check.ROOT = path.parent
        check.configured_regex = lambda: regex
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = check.main()
        return status, output.getvalue()

    def test_catch2_test_case_title_is_the_test_id(self):
        path = self.temp_file("catch.cpp",
                              'TEST_CASE("Reads real data", "[x]") { auto d = std::getenv("FO4_DATA_DIR"); }\n'
                              'TEST_CASE("Plain") { int a = 1; }\n')
        found = {test_id for _, _, test_id in check.real_data_tests([path])}
        self.assertEqual(found, {"Reads real data"})

    def test_configured_regex_unquotes_doubled_apostrophes(self):
        workflow = self.temp_file("native-ci.yml", "env:\n  REAL_DATA_TEST_REGEX: 'A''B\\.|C'\n")
        self.addCleanup(setattr, check, "WORKFLOW", check.WORKFLOW)
        check.WORKFLOW = workflow
        self.assertEqual(check.configured_regex(), "A'B\\.|C")

    def test_configured_regex_missing_line_exits(self):
        workflow = self.temp_file("native-ci.yml", "env:\n  OTHER: 'x'\n")
        self.addCleanup(setattr, check, "WORKFLOW", check.WORKFLOW)
        check.WORKFLOW = workflow
        with self.assertRaises(SystemExit) as caught:
            check.configured_regex()
        self.assertIn("REAL_DATA_TEST_REGEX not found", str(caught.exception.code))

    def test_main_fails_and_names_a_test_the_regex_does_not_select(self):
        path = self.temp_file("leaky.cpp", 'TEST(Leaky, Reads) { std::getenv("FO4_DATA_DIR"); }\n')
        status, output = self.run_main(path, "NoMatch")
        self.assertEqual(status, 1)
        self.assertIn("::error::not selected by REAL_DATA_TEST_REGEX", output)
        self.assertIn("Leaky.Reads", output)
        self.assertEqual(output.splitlines()[-1],
                         "game-data tests: 1, not selected by REAL_DATA_TEST_REGEX: 1")

    def test_main_passes_when_the_regex_selects_every_data_test(self):
        path = self.temp_file("leaky.cpp", 'TEST(Leaky, Reads) { std::getenv("FO4_DATA_DIR"); }\n')
        status, output = self.run_main(path, "Leaky\\.")
        self.assertEqual(status, 0)
        self.assertEqual(output.splitlines()[-1],
                         "game-data tests: 1, not selected by REAL_DATA_TEST_REGEX: 0")

    def test_main_exits_when_no_data_tests_are_found(self):
        path = self.temp_file("plain.cpp", "TEST(Plain, X) {}\n")
        with self.assertRaises(SystemExit) as caught:
            self.run_main(path, "Plain")
        self.assertIn("the test parser is broken", str(caught.exception.code))

    def test_workflow_regex_selects_known_real_data_names(self):
        rx = re.compile(check.configured_regex())
        for name in ("CheckErrors.VanillaMasterHasNoErrorFindings", "NifCorpus.Anything", "DialogueReal.X"):
            self.assertIsNotNone(rx.search(name), name)
        self.assertIsNone(rx.search("Plain.NoData"))


if __name__ == "__main__":
    unittest.main()
