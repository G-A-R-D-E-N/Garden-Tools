#pragma once

#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/condition_scopes.hpp"
#include "fo4/schema/ctda.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fo4::test::scope {

using Body = std::vector<std::pair<std::string, std::vector<std::byte>>>;
using Functions = std::vector<std::uint16_t>;

inline std::vector<std::byte> zstring(std::string_view text) {
  std::vector<std::byte> out;
  for (const char ch : text) out.push_back(std::byte{static_cast<std::uint8_t>(ch)});
  out.push_back(std::byte{0});
  return out;
}

inline std::vector<std::byte> ctda_bytes(std::uint16_t function) {
  fo4::test::ByteWriter ctda;
  ctda.u32(0);
  ctda.u32(0);
  ctda.u16(function);
  ctda.u16(0);
  ctda.i32(0);
  ctda.i32(0);
  ctda.u32(0);
  ctda.u32(0);
  ctda.i32(-1);
  return ctda.bytes;
}

inline fo4::Plugin make_plugin(std::string_view signature, const Body& body) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x00000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"CondRecord\0", 12});
  for (const auto& [sig, data] : body) {
    fo4::test::write_subrecord(out, sig, std::span<const std::byte>{data});
  }
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return fo4::read_plugin(out.bytes, "Cond.esp");
}

inline const fo4::Record* cond_record(const fo4::Plugin& plugin) {
  return fo4::find_record(plugin, fo4::FormKey::parse("000800:Cond.esp"));
}

inline std::optional<fo4::schema::ConditionScopeDescriptor> descriptor_for(std::string_view record,
                                                                    std::string_view path) {
  const auto scoped =
      fo4::schema::scoped_condition_path(fo4::Signature::from_string(record), path);
  if (!scoped.has_value()) return std::nullopt;
  return scoped->descriptor;
}

inline Functions functions_in(const fo4::Record& record,
                       const fo4::schema::ConditionScopeDescriptor& descriptor,
                       std::size_t index) {
  const auto read = fo4::schema::read_scoped_conditions(record, descriptor, index);
  EXPECT_TRUE(read.ok) << "scope " << index << " status " << static_cast<int>(read.error);
  Functions out;
  for (const auto& condition : read.conditions) out.push_back(condition.function);
  return out;
}

inline std::size_t index_of_nth(const fo4::Record& record, std::string_view signature, std::size_t n) {
  const auto sig = fo4::Signature::from_string(signature);
  std::size_t seen = 0;
  for (std::size_t i = 0; i < record.subrecords.size(); ++i) {
    if (record.subrecords[i].signature != sig) continue;
    if (seen++ == n) return i;
  }
  return record.subrecords.size();
}

inline void expect_prefix_unchanged(const std::vector<fo4::Subrecord>& before, const fo4::Record& after,
                             std::size_t count) {
  ASSERT_GE(after.subrecords.size(), count);
  for (std::size_t i = 0; i < count; ++i) {
    EXPECT_EQ(after.subrecords[i].signature, before[i].signature) << "subrecord " << i;
    EXPECT_EQ(after.subrecords[i].data, before[i].data) << "subrecord " << i;
  }
}

inline fo4::CtdaCondition function_condition(std::uint16_t function) {
  fo4::CtdaCondition condition;
  condition.function = function;
  return condition;
}

inline std::vector<std::string> signatures_of(const fo4::Record& record) {
  std::vector<std::string> out;
  for (const auto& sub : record.subrecords) out.push_back(sub.signature.to_string());
  return out;
}
}
