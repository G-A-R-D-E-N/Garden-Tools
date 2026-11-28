#pragma once

#include "condition_scope_fixture.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fo4::test::scope {

inline std::vector<std::byte> zeros(std::size_t count) { return std::vector<std::byte>(count); }

inline std::vector<std::uint32_t> citc_counts(const fo4::Record& record) {
  std::vector<std::uint32_t> counts;
  for (const auto& sub : record.subrecords) {
    if (sub.signature != fo4::Signature::from_string("CITC")) continue;
    std::uint32_t value = 0;
    std::memcpy(&value, sub.data.data(), sizeof(value));
    counts.push_back(value);
  }
  return counts;
}

inline void expect_suffix_unchanged(const std::vector<fo4::Subrecord>& before, const fo4::Record& after,
                             std::size_t from) {
  ASSERT_EQ(after.subrecords.size(), before.size());
  for (std::size_t i = from; i < before.size(); ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << "subrecord " << i;
  }
}

inline Functions root_functions(const fo4::Record& record) {
  const auto read = fo4::schema::read_root_conditions(record);
  EXPECT_TRUE(read.ok) << "root status " << static_cast<int>(read.error);
  Functions out;
  for (const auto& condition : read.conditions) out.push_back(condition.function);
  return out;
}

inline void expect_suffix_unchanged(const std::vector<fo4::Subrecord>& before, std::size_t before_start,
                             const fo4::Record& after, std::size_t after_start) {
  ASSERT_EQ(after.subrecords.size() - after_start, before.size() - before_start);
  for (std::size_t i = before_start; i < before.size(); ++i) {
    const auto& actual = after.subrecords[after_start + (i - before_start)];
    EXPECT_EQ(actual.signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(actual.data, before[i].data) << "subrecord " << i;
  }
}

}  // namespace fo4::test::scope
