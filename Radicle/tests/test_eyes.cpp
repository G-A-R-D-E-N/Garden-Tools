#include "fo4/forms/eyes.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> make_eyes_plugin(bool with_edid = true,
                                        bool opaque_full = false,
                                        bool embedded_nul_full = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(1);
  hedr.u32(0x801);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto group = fo4::test::begin_group(out, "EYES", 0);
  constexpr std::uint32_t record_flags = 0x00000004u | 0x00000400u;
  const auto record = fo4::test::begin_record(out, "EYES", record_flags, 0x00000800);
  if (with_edid) {
    fo4::test::write_subrecord(out, "EDID", std::string_view{"BlueEyes\0", 9});
  }
  if (opaque_full) {
    const std::array<std::byte, 4> string_id{
        std::byte{0x34}, std::byte{0x12}, std::byte{0x00}, std::byte{0x00}};
    fo4::test::write_subrecord(out, "FULL", string_id);
  } else if (embedded_nul_full) {
    const std::array<std::byte, 9> ambiguous{
        std::byte{'B'}, std::byte{'l'}, std::byte{'u'}, std::byte{'e'},
        std::byte{0}, std::byte{'X'}, std::byte{'Y'}, std::byte{'Z'}, std::byte{0}};
    fo4::test::write_subrecord(out, "FULL", ambiguous);
  } else {
    fo4::test::write_subrecord(out, "FULL", std::string_view{"Blue Eyes\0", 10});
  }
  fo4::test::write_subrecord(
      out, "ICON", std::string_view{"Textures\\Actors\\Character\\Eyes\\Blue.dds\0", 40});
  const std::array<std::byte, 1> data{std::byte{0x85}};
  fo4::test::write_subrecord(out, "DATA", data);
  const std::array<std::byte, 3> unknown{
      std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE}};
  fo4::test::write_subrecord(out, "UNAM", unknown);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

const fo4::Subrecord* find_sub(const fo4::Record& record, std::string_view name) {
  const auto signature = fo4::Signature::from_string(name);
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == signature) return &subrecord;
  }
  return nullptr;
}

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

}

TEST(Eyes, DecodesFo4FieldsAndPreservesRecordFlags) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto model = fo4::forms::decode_eyes(
      plugin, fo4::FormKey::parse("000800:EyesPatch.esp"));

  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "BlueEyes");
  ASSERT_TRUE(model.full_text.has_value());
  EXPECT_EQ(*model.full_text, "Blue Eyes");
  EXPECT_EQ(model.icon, "Textures\\Actors\\Character\\Eyes\\Blue.dds");
  EXPECT_EQ(model.data_flags, 0x85u);
  EXPECT_EQ(model.record_flags.value(), 0x00000404u);
}

TEST(Eyes, EditsTextAndFlagsWithoutTouchingUnknownSubrecordsOrHeaderFlags) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  const auto* before_record = fo4::find_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  const auto* before_unknown = find_sub(*before_record, "UNAM");
  ASSERT_NE(before_unknown, nullptr);
  const auto unknown = before_unknown->data;
  const auto header_flags = before_record->header.flags;

  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(
      plugin, key, std::string_view{"GreenEyes"}, std::string_view{"Green Eyes"},
      std::string_view{"Textures\\Eyes\\Green.dds"}));
  ASSERT_TRUE(fo4::forms::set_eyes_data_flags(plugin, key, 0x07));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  const auto model = fo4::forms::decode_eyes(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "GreenEyes");
  ASSERT_TRUE(model.full_text.has_value());
  EXPECT_EQ(*model.full_text, "Green Eyes");
  EXPECT_EQ(model.icon, "Textures\\Eyes\\Green.dds");
  EXPECT_EQ(model.data_flags, 0x07u);
  EXPECT_EQ(model.record_flags, header_flags);

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  const auto* after_unknown = find_sub(*record, "UNAM");
  ASSERT_NE(after_unknown, nullptr);
  EXPECT_EQ(after_unknown->data, unknown);
}

TEST(Eyes, CanInsertOptionalEdidBeforeFull) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(false), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  ASSERT_FALSE(fo4::forms::decode_eyes(plugin, key).editor_id.has_value());

  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(
      plugin, key, std::string_view{"InsertedEyes"}, std::nullopt, std::nullopt));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  const auto model = fo4::forms::decode_eyes(reread, key);
  ASSERT_TRUE(model.editor_id.has_value());
  EXPECT_EQ(*model.editor_id, "InsertedEyes");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  ASSERT_GE(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
}

TEST(Eyes, FourByteFullStaysOpaqueAndCannotBeReinterpretedAsText) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(true, true), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  const auto model = fo4::forms::decode_eyes(plugin, key);
  EXPECT_FALSE(model.full_text.has_value());
  ASSERT_EQ(model.full_raw.size(), 4u);
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_THROW(
      fo4::forms::set_eyes_text_fields(
          plugin, key, std::nullopt, std::string_view{"Unsafe Guess"}, std::nullopt),
      fo4::forms::EyesError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Eyes, EmbeddedNulFullStaysOpaqueAndCannotEraseTrailingBytes) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(true, false, true), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  const auto model = fo4::forms::decode_eyes(plugin, key);
  EXPECT_FALSE(model.full_text.has_value());
  ASSERT_EQ(model.full_raw.size(), 9u);
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_THROW(
      fo4::forms::set_eyes_text_fields(
          plugin, key, std::nullopt, std::string_view{"Unsafe Rewrite"}, std::nullopt),
      fo4::forms::EyesError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Eyes, NonAsciiEditorIdIsStoredAsWindows1252AndRepeatEditStaysClean) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(
      plugin, key, std::string_view{"Caf\xC3\xA9"}, std::nullopt, std::nullopt));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  EXPECT_EQ(fo4::forms::decode_eyes(reread, key).editor_id,
            std::optional<std::string>{"Caf\xE9"});

  const auto original = fo4::test::original_bytes(plugin);
  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(
      plugin, key, std::string_view{"Caf\xC3\xA9"}, std::nullopt, std::nullopt));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Eyes, NonAsciiFullAndIconAreStoredAsWindows1252AndRepeatEditStaysClean) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(plugin, key, std::nullopt,
                                               std::string_view{"Caf\xC3\xA9"},
                                               std::string_view{"Ic\xC3\xA9n"}));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  const auto decoded = fo4::forms::decode_eyes(reread, key);
  EXPECT_EQ(decoded.full_text, std::optional<std::string>{"Caf\xE9"});
  EXPECT_EQ(decoded.icon, "Ic\xE9n");

  const auto original = fo4::test::original_bytes(plugin);
  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(plugin, key, std::nullopt,
                                               std::string_view{"Caf\xC3\xA9"},
                                               std::string_view{"Ic\xC3\xA9n"}));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

TEST(Eyes, NoOpEditDoesNotDirtyParsedPlugin) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  ASSERT_TRUE(fo4::forms::set_eyes_text_fields(
      plugin, key, std::string_view{"BlueEyes"}, std::string_view{"Blue Eyes"},
      std::string_view{"Textures\\Actors\\Character\\Eyes\\Blue.dds"}));
  ASSERT_TRUE(fo4::forms::set_eyes_data_flags(plugin, key, 0x85));
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

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

TEST(EyesCreation, StoresEditorIdAsWindows1252) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");

  const auto key = fo4::forms::create_eyes(
      plugin,
      std::string_view{"Caf\xC3\xA9"},
      "Created Eyes",
      "Textures\\Eyes\\Created.dds",
      0u);

  const auto* record = fo4::find_record(plugin, key);
  ASSERT_NE(record, nullptr);
  ASSERT_FALSE(record->subrecords.empty());
  ASSERT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  const std::vector<std::byte> expected{
      std::byte{'C'}, std::byte{'a'}, std::byte{'f'}, std::byte{0xE9}, std::byte{0}};
  EXPECT_EQ(record->subrecords[0].data, expected);
}

TEST(EyesCreation, EditorIdOutsideWindows1252IsRefusedBeforeAnyMutation) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_eyes(
          plugin,
          std::string_view{"Snow\xE2\x98\x83"},
          "Created Eyes",
          "Textures\\Eyes\\Created.dds",
          0u)),
      fo4::FormIdError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}

TEST(EyesCreation, InvalidInputDoesNotConsumeAFormId) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  const auto original = fo4::test::original_bytes(plugin);

  const std::string_view bad_full{"Bad\0Name", 8};
  EXPECT_THROW(
      static_cast<void>(fo4::forms::create_eyes(
          plugin, std::nullopt, bad_full, "Textures\\Eyes\\Created.dds", 0u)),
      fo4::forms::EyesError);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  EXPECT_EQ(plugin.header.next_form_id, 0x800u);
  EXPECT_TRUE(plugin.groups.empty());
}
