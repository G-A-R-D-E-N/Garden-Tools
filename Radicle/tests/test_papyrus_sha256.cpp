#include "../src/papyrus/sha256.hpp"

#include "sha256_vectors.hpp"

#include <gtest/gtest.h>

#include <string_view>

// Detects (invariant): the Papyrus sha256Hex wrapper must return the same lower-case digests as the io hash.
TEST(PapyrusSha256, MatchesKnownAnswers) {
  for (const auto& vector : fo4::test::sha256::known_answers()) {
    EXPECT_EQ(fo4::papyrus::sha256Hex(vector.input), vector.digest) << vector.label;
  }
}

// Detects (invariant): a default string_view (null data) hashes as empty input.
TEST(PapyrusSha256, HashesANullViewAsEmptyInput) {
  EXPECT_EQ(fo4::papyrus::sha256Hex(std::string_view{}),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
