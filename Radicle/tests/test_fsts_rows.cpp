#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::byte>;
using fo4::test::ByteWriter;

constexpr std::string_view k_key = "000800:Patch.esp";

Bytes words(std::initializer_list<std::uint32_t> values) {
  ByteWriter out;
  for (const auto value : values) out.u32(value);
  return out.bytes;
}

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes make_plugin(std::initializer_list<std::uint32_t> counts) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "FSTS", 0);
  const auto record = fo4::test::begin_record(out, "FSTS", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleSteps"));
  fo4::test::write_subrecord(out, "XCNT", words(counts));
  fo4::test::write_subrecord(out, "DATA", words({0x815, 0x814, 0x812, 0x813, 0x811}));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("FSTS"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(FstsRows, XcntCountRowsUseTheXEditStructLayout) {
  const auto walking = row("FSTS\\WalkingCount");
  const auto running = row("FSTS\\RunningCount");
  const auto sprinting = row("FSTS\\SprintingCount");
  const auto sneaking = row("FSTS\\SneakingCount");
  const auto swimming = row("FSTS\\SwimmingCount");

  for (const auto* field : {&walking, &running, &sprinting, &sneaking, &swimming}) {
    EXPECT_EQ(field->scalar.subrecord, fo4::Signature::from_string("XCNT"));
    EXPECT_EQ(field->scalar.storage, fo4::schema::ScalarStorage::u32);
    EXPECT_TRUE(field->scalar.embedded);
    EXPECT_EQ(field->scalar.payload_size, std::size_t{20});
    EXPECT_EQ(field->scalar.minimum, 0.0);
    EXPECT_EQ(field->scalar.maximum, 4294967295.0);
    EXPECT_FALSE(field->scalar.address.repeated);
  }

  EXPECT_EQ(walking.scalar.offset, std::size_t{0});
  EXPECT_EQ(running.scalar.offset, std::size_t{4});
  EXPECT_EQ(sprinting.scalar.offset, std::size_t{8});
  EXPECT_EQ(sneaking.scalar.offset, std::size_t{12});
  EXPECT_EQ(swimming.scalar.offset, std::size_t{16});

  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("XCNT")).has_value());
}

TEST(FstsRows, ReadsTheFiveCategoryCounts) {
  const auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  const std::array<std::pair<std::string_view, std::string_view>, 5> expected{{
      {"FSTS\\WalkingCount", "1"},
      {"FSTS\\RunningCount", "2"},
      {"FSTS\\SprintingCount", "0"},
      {"FSTS\\SneakingCount", "1"},
      {"FSTS\\SwimmingCount", "1"},
  }};
  for (const auto& [path, value] : expected) {
    const auto field = row(path);
    EXPECT_EQ(fo4::record_field_count(plugin, record, field), std::optional<std::size_t>{1}) << path;
    const auto* found = fo4::find_record(plugin, record);
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(found->subrecords.size(), std::size_t{3});
    const auto bytes = fo4::schema::scalar_field_bytes(found->subrecords[1].data, field.scalar);
    EXPECT_EQ(fo4::schema::decode_scalar(bytes, field.scalar), value) << path;
  }

  EXPECT_EQ(fo4::record_field_count(plugin, record, "FSTS\\Footsteps"), std::optional<std::size_t>{5});
}

TEST(FstsRows, RefusesGenericEditsOfTheDerivedCounts) {
  auto plugin = fo4::read_plugin(make_plugin({1, 2, 0, 1, 1}), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);

  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("FSTS\\WalkingCount"), 0, "2"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("FSTS\\SwimmingCount"), 0, "3"),
               fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_string(plugin, record, row("FSTS\\SneakingCount"), 0, "1"),
               fo4::schema::ScalarError);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* found = fo4::find_record(reread, record);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->subrecords[1].data, words({1, 2, 0, 1, 1}));
}

TEST(FstsRows, ShortXcntPayloadsHideTheCountsPastTheirEnd) {
  const auto sneaking = row("FSTS\\SneakingCount");
  const auto swimming = row("FSTS\\SwimmingCount");

  const auto twelve = words({1, 2, 0});
  const auto twenty = words({1, 2, 0, 1, 1});
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(twelve, sneaking.scalar).empty());
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(twelve, swimming.scalar).empty());
  EXPECT_EQ(fo4::schema::scalar_field_bytes(twenty, sneaking.scalar).size(), std::size_t{4});
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(twenty, swimming.scalar),
                                       swimming.scalar),
            "1");
}
