#include "fo4/forms/eyes.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_empty_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(0x800);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  return out.bytes;
}

}  // namespace

TEST(EyesCreation, CreatesRequiredStructureThroughRecordLifecycleAndRoundTrips) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  const fo4::RecordFlags flags{0x00000404u};

  const auto key = fo4::forms::create_eyes(
      plugin,
      std::string_view{"CreatedEyes"},
      "Created Eyes",
      "Textures\\Eyes\\Created.dds",
      0x85u,
      flags);

  EXPECT_EQ(key, fo4::FormKey::parse("000800:EyesPatch.esp"));
  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  ASSERT_EQ(plugin.groups.size(), 1u);
  EXPECT_EQ(plugin.groups[0].header.group_type, 0);
  EXPECT_EQ(plugin.groups[0].header.top_label().to_string(), "EYES");

  const auto bytes = fo4::write_plugin(plugin);
  const auto reread = fo4::read_plugin(bytes, "EyesPatch.esp");
  EXPECT_EQ(reread.header.next_form_id, 0x801u);
  EXPECT_EQ(reread.header.record_count, 2u);  // EYES GRUP + EYES major record.

  const auto model = fo4::forms::decode_eyes(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "CreatedEyes");
  ASSERT_TRUE(model.full_text.has_value());
  EXPECT_EQ(*model.full_text, "Created Eyes");
  EXPECT_EQ(model.icon, "Textures\\Eyes\\Created.dds");
  EXPECT_EQ(model.data_flags, 0x85u);
  EXPECT_EQ(model.record_flags, flags);

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 4u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "ICON");
  EXPECT_EQ(record->subrecords[3].signature.to_string(), "DATA");
}

TEST(EyesCreation, InvalidInputDoesNotConsumeAFormId) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  const auto original = plugin.original_bytes;

  const std::string_view bad_full{"Bad\0Name", 8};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_eyes(
          plugin, std::nullopt, bad_full, "Textures\\Eyes\\Created.dds", 0u)),
      fo4::forms::EyesError);
  EXPECT_EQ(plugin.original_bytes, original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}
