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


if __name__ == "__main__":
    unittest.main()
