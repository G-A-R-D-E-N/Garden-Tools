#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_plugin(bool include_xhlt = true,
                                   bool duplicate_xhlt = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "REFR", 0);
  const auto record = fo4::test::begin_record(out, "REFR", 0, 0x00000800);
  if (include_xhlt) {
    fo4::test::ByteWriter health;
    health.u32(50);
    fo4::test::write_subrecord(out, "XHLT", health.bytes);
    if (duplicate_xhlt) {
      fo4::test::ByteWriter second;
      second.u32(75);
      fo4::test::write_subrecord(out, "XHLT", second.bytes);
    }
  }
  const std::array<std::byte, 3> unknown{
      std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  fo4::test::write_subrecord(out, "ZZZZ", unknown);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view signature) {
  const auto wanted = fo4::Signature::from_string(signature);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == wanted) return &subrecord;
  }
  return nullptr;
}

}

TEST(ScalarMutation, UpdatesExistingXhltAndRoundTripsBinary) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto key = fo4::FormKey::parse("000800:Patch.esp");

  EXPECT_EQ(
      fo4::set_record_scalar(
          plugin, key, fo4::Signature::from_string("XHLT"), "75"),
      fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "Patch.esp");
  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* xhlt = find_sub(*record, "XHLT");
  ASSERT_NE(xhlt, nullptr);
  ASSERT_EQ(xhlt->data.size(), 4u);
  EXPECT_EQ(std::to_integer<unsigned>(xhlt->data[0]), 75u);
  EXPECT_EQ(std::to_integer<unsigned>(xhlt->data[1]), 0u);
  EXPECT_EQ(std::to_integer<unsigned>(xhlt->data[2]), 0u);
  EXPECT_EQ(std::to_integer<unsigned>(xhlt->data[3]), 0u);

  const auto* unknown = find_sub(*record, "ZZZZ");
  ASSERT_NE(unknown, nullptr);
  EXPECT_EQ(unknown->data,
            (std::vector<std::byte>{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}}));
}

TEST(ScalarMutation, NoOpKeepsOriginalPluginBytes) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto status = fo4::set_record_scalar(
      plugin, fo4::FormKey::parse("000800:Patch.esp"),
      fo4::Signature::from_string("XHLT"), "50");
  EXPECT_EQ(status, fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(ScalarMutation, RefusesMissingScalarSubrecordInsteadOfGuessingOrder) {
  auto plugin = fo4::read_plugin(make_plugin(false), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  const auto status = fo4::set_record_scalar(
      plugin, fo4::FormKey::parse("000800:Patch.esp"),
      fo4::Signature::from_string("XHLT"), "50");
  EXPECT_EQ(status, fo4::ScalarMutationStatus::subrecord_missing);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(ScalarMutation, RejectsDuplicateScalarSubrecordsWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(true, true), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(
      fo4::set_record_scalar(
          plugin, fo4::FormKey::parse("000800:Patch.esp"),
          fo4::Signature::from_string("XHLT"), "25"),
      fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(ScalarMutation, RejectsInvalidPercentWithoutMutation) {
  auto plugin = fo4::read_plugin(make_plugin(), "Patch.esp");
  const auto original = fo4::test::original_bytes(plugin);
  EXPECT_THROW(
      fo4::set_record_scalar(
          plugin, fo4::FormKey::parse("000800:Patch.esp"),
          fo4::Signature::from_string("XHLT"), "101"),
      fo4::schema::ScalarError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}
