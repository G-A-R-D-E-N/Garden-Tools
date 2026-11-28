#include "fo4/io/sha256.hpp"

#include "sha256_vectors.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>

// Detects (invariant): sha256_hex must match the FIPS 180-4 digest for empty input and for the padding boundary lengths.
TEST(IoSha256, MatchesKnownAnswers) {
  for (const auto& vector : fo4::test::sha256::known_answers()) {
    const auto bytes = std::as_bytes(std::span(vector.input.data(), vector.input.size()));
    EXPECT_EQ(fo4::io::sha256_hex(bytes), vector.digest) << vector.label;
  }
}

// Detects (invariant): a default-constructed empty span (null data) hashes as empty input.
TEST(IoSha256, HashesAnEmptySpanAsEmptyInput) {
  EXPECT_EQ(fo4::io::sha256_hex(std::span<const std::byte>{}),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}
