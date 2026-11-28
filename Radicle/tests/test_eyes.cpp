#include "fo4/forms/record_flags.hpp"
#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/subrecord_authoring.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/localized_fields.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

fo4::schema::ScalarFieldDescriptor eyes_row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(path);
  if (!field) throw std::invalid_argument("unknown scalar row " + std::string(path));
  return *field;
}

std::optional<std::string> editor_id_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  const auto* edid = record == nullptr ? nullptr : find_sub(*record, "EDID");
  if (edid == nullptr) return std::nullopt;
  return std::string(reinterpret_cast<const char*>(edid->data.data()), edid->data.size() - 1);
}

std::optional<std::string> full_text_of(const fo4::Plugin& plugin, const fo4::FormKey& key) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return std::nullopt;
  const auto name = fo4::record_full_name(plugin, *record);
  if (name.state != fo4::RecordNameState::present) return std::nullopt;
  return name.value.text;
}

std::string row_text(const fo4::Plugin& plugin, const fo4::FormKey& key, std::string_view path) {
  const auto* record = fo4::find_record(plugin, key);
  if (record == nullptr) return {};
  return fo4::record_scalar_text(plugin, *record, eyes_row(path), 0).value_or("");
}

fo4::schema::LocalizedString inline_name(std::string text) {
  fo4::schema::LocalizedString name;
  name.text = std::move(text);
  return name;
}

}

// Constraint: detects an EYES row reading the wrong subrecord, or an edit through the generic writers changing the record flags or the unknown UNAM subrecord.
TEST(Eyes, GenericRowsReadAndEditWithoutTouchingUnknownSubrecordsOrHeaderFlags) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  const auto* before_record = fo4::find_record(plugin, key);
  ASSERT_NE(before_record, nullptr);
  const auto* before_unknown = find_sub(*before_record, "UNAM");
  ASSERT_NE(before_unknown, nullptr);
  const auto unknown = before_unknown->data;
  const auto header_flags = before_record->header.flags;
  EXPECT_EQ(header_flags.value(), 0x00000404u);

  EXPECT_EQ(editor_id_of(plugin, key), std::optional<std::string>{"BlueEyes"});
  EXPECT_EQ(full_text_of(plugin, key), std::optional<std::string>{"Blue Eyes"});
  EXPECT_EQ(row_text(plugin, key, "EYES\\Texture"), "Textures\\Actors\\Character\\Eyes\\Blue.dds");
  EXPECT_EQ(row_text(plugin, key, "EYES\\Flags"), "133");

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "GreenEyes"));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key, inline_name("Green Eyes")),
            fo4::RecordNameStatus::updated);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "EYES\\Texture", 0, "Textures\\Eyes\\Green.dds"),
            fo4::ScalarMutationStatus::updated);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, eyes_row("EYES\\Flags"), 0, "7"),
            fo4::ScalarMutationStatus::updated);

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  EXPECT_EQ(editor_id_of(reread, key), std::optional<std::string>{"GreenEyes"});
  EXPECT_EQ(full_text_of(reread, key), std::optional<std::string>{"Green Eyes"});
  EXPECT_EQ(row_text(reread, key, "EYES\\Texture"), "Textures\\Eyes\\Green.dds");
  EXPECT_EQ(row_text(reread, key, "EYES\\Flags"), "7");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.flags, header_flags);
  const auto* after_unknown = find_sub(*record, "UNAM");
  ASSERT_NE(after_unknown, nullptr);
  EXPECT_EQ(after_unknown->data, unknown);
}

// Constraint: detects an optional EDID inserted after FULL instead of ahead of it when an EYES record had none.
TEST(Eyes, SetEditorIdInsertsAnAbsentEdidBeforeFull) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(false), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  ASSERT_FALSE(editor_id_of(plugin, key).has_value());

  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "InsertedEyes"));
  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  EXPECT_EQ(editor_id_of(reread, key), std::optional<std::string>{"InsertedEyes"});

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  ASSERT_GE(record->subrecords.size(), 2u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
}

// Constraint: detects a four-byte string ID or an embedded-NUL FULL being rewritten as text through the name setter or the Name row, which would erase the bytes after the first NUL.
TEST(Eyes, OpaqueFullIsRefusedByTheNameSetterAndTheNameRowWithoutChangingBytes) {
  const std::array<std::pair<bool, bool>, 2> shapes{std::pair{true, false}, std::pair{false, true}};
  for (const auto& [opaque_full, embedded_nul_full] : shapes) {
    auto plugin = fo4::read_plugin(make_eyes_plugin(true, opaque_full, embedded_nul_full), "EyesPatch.esp");
    const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
    const auto original = fo4::test::original_bytes(plugin);

    EXPECT_EQ(fo4::set_record_full_name(plugin, key, inline_name("Unsafe Rewrite")),
              fo4::RecordNameStatus::opaque_existing);
    EXPECT_THROW(static_cast<void>(fo4::set_record_string(plugin, key, "EYES\\Name", 0, "Unsafe Rewrite")),
                 fo4::schema::ScalarError);
    EXPECT_EQ(fo4::test::original_bytes(plugin), original);
  }
}

// Constraint: detects a non-ASCII EDID stored as UTF-8 instead of Windows-1252, and a repeated identical edit of an EYES text row or the flags dirtying the parsed plugin.
TEST(Eyes, NonAsciiEditorIdIsStoredAsWindows1252AndRepeatEditsStayClean) {
  auto plugin = fo4::read_plugin(make_eyes_plugin(), "EyesPatch.esp");
  const auto key = fo4::FormKey::parse("000800:EyesPatch.esp");
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "Caf\xC3\xA9"));

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  EXPECT_EQ(editor_id_of(reread, key), std::optional<std::string>{"Caf\xE9"});

  const auto original = fo4::test::original_bytes(plugin);
  ASSERT_TRUE(fo4::set_record_editor_id(plugin, key, "Caf\xC3\xA9"));
  EXPECT_EQ(fo4::set_record_full_name(plugin, key, inline_name("Blue Eyes")),
            fo4::RecordNameStatus::unchanged);
  EXPECT_EQ(fo4::set_record_string(plugin, key, "EYES\\Texture", 0,
                                   "Textures\\Actors\\Character\\Eyes\\Blue.dds"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::set_record_scalar(plugin, key, eyes_row("EYES\\Flags"), 0, "133"),
            fo4::ScalarMutationStatus::unchanged);
  EXPECT_EQ(fo4::test::original_bytes(plugin), original);
}

// Constraint: detects the generic EYES create dropping a required subrecord or writing the group or next ID wrongly, or the Name, Texture, Flags and record-flag writers failing on the fresh record.
TEST(EyesCreation, GenericCreateBuildsTheRequiredStructureAndTheRowsFillIt) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");

  const auto created = fo4::create_record(plugin, fo4::Signature::from_string("EYES"), "CreatedEyes");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto key = created.key;
  EXPECT_EQ(key, fo4::FormKey::parse("000800:EyesPatch.esp"));
  ASSERT_EQ(fo4::set_record_string(plugin, key, "EYES\\Name", 0, "Created Eyes"),
            fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_string(plugin, key, "EYES\\Texture", 0, "Textures\\Eyes\\Created.dds"),
            fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_scalar(plugin, key, eyes_row("EYES\\Flags"), 0, "133"),
            fo4::ScalarMutationStatus::updated);
  ASSERT_EQ(fo4::set_record_flags(plugin, key, 0x00000404u), fo4::RecordFlagStatus::changed);

  EXPECT_EQ(plugin.header.next_form_id, 0x801u);
  ASSERT_EQ(plugin.groups.size(), 1u);
  EXPECT_EQ(plugin.groups[0].header.group_type, 0);
  EXPECT_EQ(plugin.groups[0].header.top_label().to_string(), "EYES");

  const auto reread = fo4::read_plugin(fo4::write_plugin(plugin), "EyesPatch.esp");
  EXPECT_EQ(reread.header.next_form_id, 0x801u);
  EXPECT_EQ(reread.header.record_count, 2u);
  EXPECT_EQ(editor_id_of(reread, key), std::optional<std::string>{"CreatedEyes"});
  EXPECT_EQ(full_text_of(reread, key), std::optional<std::string>{"Created Eyes"});
  EXPECT_EQ(row_text(reread, key, "EYES\\Texture"), "Textures\\Eyes\\Created.dds");
  EXPECT_EQ(row_text(reread, key, "EYES\\Flags"), "133");

  const auto* record = fo4::find_record(reread, key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->header.flags.value(), 0x00000404u);
  ASSERT_EQ(record->subrecords.size(), 4u);
  EXPECT_EQ(record->subrecords[0].signature.to_string(), "EDID");
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "FULL");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "ICON");
  EXPECT_EQ(record->subrecords[3].signature.to_string(), "DATA");
}

// Constraint: detects an EDID written for an empty editor ID, and an inline FULL created in a localized plugin where a string ID belongs.
TEST(EyesCreation, GenericCreateWritesNoEdidForAnEmptyIdAndRefusesALocalizedPlugin) {
  auto plugin = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  const auto created = fo4::create_record(plugin, fo4::Signature::from_string("EYES"), "");
  ASSERT_EQ(created.status, fo4::RecordCreateStatus::created);
  const auto* record = fo4::find_record(plugin, created.key);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(find_sub(*record, "EDID"), nullptr);

  auto localized = fo4::read_plugin(make_empty_plugin(), "EyesPatch.esp");
  localized.header.tes4_flags =
      fo4::RecordFlags{localized.header.tes4_flags.value() | fo4::RecordFlags::localized};
  const auto before = localized.header.next_form_id;
  EXPECT_EQ(fo4::create_record(localized, fo4::Signature::from_string("EYES"), "X").status,
            fo4::RecordCreateStatus::no_default);
  EXPECT_EQ(localized.header.next_form_id, before);
  EXPECT_TRUE(localized.groups.empty());
}
