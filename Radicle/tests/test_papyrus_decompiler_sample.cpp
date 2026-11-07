#include "papyrus_corpus_roots.hpp"

#include <gtest/gtest-spi.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

using fo4::papyrus::test::stratifiedSample;

namespace {

const std::filesystem::path kSampleRoot = "/corpus";

std::vector<std::filesystem::path> scriptsUnderRoot(std::initializer_list<const char*> names) {
  std::vector<std::filesystem::path> files;
  for (const char* name : names) files.push_back(kSampleRoot / name);
  return files;
}

std::vector<std::string> sampledNames(const std::vector<std::filesystem::path>& files) {
  std::vector<std::string> names;
  for (const auto& file : files) names.push_back(file.generic_string());
  return names;
}

}  // namespace

TEST(PapyrusDecompilerSample, EveryKthFileInCaseInsensitiveOrderSpansTheCorpus) {
  const auto sample = stratifiedSample(
      scriptsUnderRoot({"Zeta.psc", "alpha.psc", "Mid/beta.psc", "gamma.psc", "Delta/one.psc",
                        "epsilon.psc", "Kappa.psc", "lambda/x.psc", "omega.psc", "Beta.psc"}),
      {kSampleRoot}, 3);
  EXPECT_EQ(sample.stride, 4u);
  const std::vector<std::string> every4th{"/corpus/alpha.psc", "/corpus/gamma.psc",
                                          "/corpus/omega.psc"};
  EXPECT_EQ(sampledNames(sample.files), every4th);
}

TEST(PapyrusDecompilerSample, FilesThatFitTheLimitAreAllTakenWithStrideOne) {
  const auto files = scriptsUnderRoot({"delta.psc", "Charlie.psc", "bravo.psc", "Alpha.psc"});
  const std::vector<std::string> ordered{"/corpus/Alpha.psc", "/corpus/bravo.psc",
                                         "/corpus/Charlie.psc", "/corpus/delta.psc"};
  for (const std::size_t limit : {4u, 9u, 0u}) {
    const auto sample = stratifiedSample(files, {kSampleRoot}, limit);
    EXPECT_EQ(sample.stride, 1u) << "limit " << limit;
    EXPECT_EQ(sampledNames(sample.files), ordered) << "limit " << limit;
  }
}

TEST(PapyrusDecompilerSample, SelectionDoesNotDependOnDirectoryEnumerationOrder) {
  auto reversed = scriptsUnderRoot({"Zeta.psc", "alpha.psc", "Mid/beta.psc", "gamma.psc",
                                    "Delta/one.psc", "epsilon.psc", "Kappa.psc", "lambda/x.psc",
                                    "omega.psc", "Beta.psc"});
  const auto forward = stratifiedSample(reversed, {kSampleRoot}, 3);
  std::reverse(reversed.begin(), reversed.end());
  const auto backward = stratifiedSample(reversed, {kSampleRoot}, 3);
  EXPECT_EQ(backward.stride, forward.stride);
  EXPECT_EQ(sampledNames(backward.files), sampledNames(forward.files));
}

namespace {

void unsetCorpusFloor() {
#ifdef _WIN32
  _putenv_s("FO4RE_PEX_MIN_PAIRS", "");
#else
  unsetenv("FO4RE_PEX_MIN_PAIRS");
#endif
}

void setCorpusFloor(const char* value) {
#ifdef _WIN32
  _putenv_s("FO4RE_PEX_MIN_PAIRS", value);
#else
  setenv("FO4RE_PEX_MIN_PAIRS", value, 1);
#endif
}

}  // namespace

TEST(PapyrusCorpusFloor, OnlyAnUnsetOrEmptyValueFallsBackToTheDefault) {
  const auto parse = [](const char* value) {
    setCorpusFloor(value);
    return fo4::papyrus::test::corpusFloor("FO4RE_PEX_MIN_PAIRS", 7);
  };
  unsetCorpusFloor();
  EXPECT_EQ(fo4::papyrus::test::corpusFloor("FO4RE_PEX_MIN_PAIRS", 7), 7u);
  EXPECT_EQ(parse(""), 7u);
  EXPECT_EQ(parse("200"), 200u);
  std::size_t floor = 0;
  for (const char* bad : {"0", "-5", "150abc", "abc"}) {
    EXPECT_NONFATAL_FAILURE(floor = parse(bad), "must be a positive integer");
    EXPECT_EQ(floor, 7u) << bad;
  }
  unsetCorpusFloor();
}
