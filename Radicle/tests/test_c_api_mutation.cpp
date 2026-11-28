#include "fo4/c_api_env.h"
#include "fo4/c_api_object_template.h"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Plugin {
  radicle_plugin* handle{nullptr};
  std::filesystem::path dir;
  std::filesystem::path input;
  explicit Plugin(const char* filename) {
    EXPECT_EQ(radicle_plugin_create(filename, &handle), RADICLE_STATUS_OK);
  }
  explicit Plugin(const std::vector<std::byte>& bytes, const char* filename) {
    dir = std::filesystem::temp_directory_path() /
          (std::string("radicle_c_mutation_") + filename +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    input = dir / filename;
    std::ofstream stream(input, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    stream.close();
    EXPECT_EQ(radicle_plugin_read_file(input.string().c_str(), &handle), RADICLE_STATUS_OK);
  }
  ~Plugin() {
    radicle_plugin_destroy(handle);
    std::error_code ec;
    if (!dir.empty()) std::filesystem::remove_all(dir, ec);
  }
  Plugin(const Plugin&) = delete;
  Plugin& operator=(const Plugin&) = delete;
};

std::vector<std::byte> misc_bytes() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto record = fo4::test::begin_record(out, "MISC", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view("Item\0", 5));
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

std::vector<std::byte> template_bytes(bool empty = false, bool trailing = false, bool ambiguous = false) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f); hedr.u32(2); hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esm\0", 11});
  fo4::test::ByteWriter master_data; master_data.u32(0); master_data.u32(0);
  fo4::test::write_subrecord(out, "DATA", master_data.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "FURN", 0);
  const auto record = fo4::test::begin_record(out, "FURN", 0, 0x01000800);
  fo4::test::ByteWriter count; count.u32(ambiguous ? 2 : 1);
  fo4::test::write_subrecord(out, "OBTE", count.bytes);
  fo4::test::ByteWriter body;
  if (empty) {
    body.u32(0); body.u32(0);
    for (int i = 0; i < 10; ++i) body.u8(0);
  } else {
    body.u32(1); body.u32(1);
    for (int i = 0; i < 7; ++i) body.u8(0);
    body.u8(1); body.u32(0x100); body.u8(0); body.u8(0);
    body.u32(0x200); body.u8(0); body.u8(1); body.u8(0);
    body.u8(4); for (int i = 1; i < 12; ++i) body.u8(0);
    body.u32(0x300); body.u32(0); body.u32(0);
  }
  fo4::test::write_subrecord(out, "OBTS", body.bytes);
  if (ambiguous) {
    fo4::test::write_subrecord(out, "FULL", std::string_view{"Name\0", 5});
    fo4::test::write_subrecord(out, "OBTS", body.bytes);
  }
  if (trailing) {
    fo4::test::write_subrecord(out, "FULL", std::string_view{"Name\0", 5});
    fo4::test::write_subrecord(out, "OBTF", std::string_view{});
  }
  fo4::test::write_subrecord(out, "STOP", std::string_view{});
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

// Detects: the C ABI refused every multi-combination template as ambiguous, so the row must be editable.
TEST(CApiMutation, ObjectTemplateMarkerBetweenCombinationsEditsTheSecond) {
  Plugin plugin(template_bytes(false, false, true), "Ambiguous.esp");
  ASSERT_NE(plugin.handle, nullptr);
  const radicle_form_key record{"Ambiguous.esp", 0x800};
  const radicle_form_key target{"Master.esm", 0x555};
  radicle_object_template_write status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 1,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, target, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);

  uint8_t* after_data = nullptr;
  size_t after_size = 0;
  ASSERT_EQ(radicle_plugin_write_bytes(plugin.handle, &after_data, &after_size), RADICLE_STATUS_OK);
  std::vector<std::byte> saved;
  saved.reserve(after_size);
  for (size_t i = 0; i < after_size; ++i) saved.push_back(static_cast<std::byte>(after_data[i]));
  radicle_free_bytes(after_data);
  const auto reread = fo4::read_plugin(saved, "Ambiguous.esp");
  const auto* reread_record = fo4::find_record(reread, fo4::FormKey::parse("000800:Ambiguous.esp"));
  ASSERT_NE(reread_record, nullptr);
  std::size_t seen = 0;
  for (const auto& sub : reread_record->subrecords) {
    if (sub.signature != fo4::Signature::from_string("OBTS")) continue;
    std::uint32_t keyword = 0;
    std::memcpy(&keyword, sub.data.data() + 0x10, 4);
    if (seen == 1) { EXPECT_EQ(keyword, 0x00000555u) << "the edit missed the second combination"; }
    else { EXPECT_EQ(keyword, 0x00000100u) << "the first combination changed"; }
    ++seen;
  }
  EXPECT_EQ(seen, 2u);
}

// Detects: C callers must preserve trailing unordered members when editing a single combination.
TEST(CApiMutation, ObjectTemplateUnorderedMembersSurviveSave) {
  Plugin plugin(template_bytes(false, true), "Unordered.esp");
  ASSERT_NE(plugin.handle, nullptr);
  radicle_object_template_write status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, {"Unordered.esp", 0x800}, 0,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, {"Master.esm", 0x555}, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);
  const auto output = plugin.dir / "out.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.handle, output.string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(output);
  const auto* record = fo4::find_record(saved, fo4::FormKey::parse("000800:out.esp"));
  ASSERT_NE(record, nullptr);
  ASSERT_EQ(record->subrecords.size(), 5u);
  EXPECT_EQ(record->subrecords[1].signature.to_string(), "OBTS");
  EXPECT_EQ(record->subrecords[2].signature.to_string(), "FULL");
  EXPECT_EQ(record->subrecords[3].signature.to_string(), "OBTF");
  EXPECT_EQ(record->subrecords[1].data[16], std::byte{0x55});
}

// Detects: C callers must be able to seed every empty nested section using explicit input.
TEST(CApiMutation, ObjectTemplateExplicitRowsSeedEmptySections) {
  Plugin plugin(template_bytes(true), "Empty.esp");
  ASSERT_NE(plugin.handle, nullptr);
  const radicle_form_key record{"Empty.esp", 0x800};
  radicle_object_template_insert_row row{};
  row.link = {"Master.esm", 0x555};
  row.property.value_type = 0;
  row.property.value1 = 77;
  radicle_object_template_write status = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
  for (const auto kind : {RADICLE_OBJECT_TEMPLATE_KEYWORD, RADICLE_OBJECT_TEMPLATE_INCLUDE,
                          RADICLE_OBJECT_TEMPLATE_PROPERTY}) {
    ASSERT_EQ(radicle_plugin_insert_object_template_row(plugin.handle, record, 0, kind,
        0, &row, &status), RADICLE_STATUS_OK);
    EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);
  }
  row.property.value_type = 3;
  row.property.value1 = 0x0042ff41;
  row.property.value2 = 0xdeadbeef;
  ASSERT_EQ(radicle_plugin_insert_object_template_row(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_PROPERTY, 1, &row, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(status, RADICLE_OBJECT_TEMPLATE_INVALID_VALUE);
  row.property.value2 = 0;
  ASSERT_EQ(radicle_plugin_insert_object_template_row(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_PROPERTY, 1, &row, &status), RADICLE_STATUS_OK);
  ASSERT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_property_value(plugin.handle, record, 0, 1,
      0, 0xff004241, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_property_value(plugin.handle, record, 0, 1,
      1, 0xdeadbeef, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_INVALID_VALUE);
  radicle_object_template_property replacement{};
  replacement.value_type = 3; replacement.function_type = 2; replacement.property = 9;
  replacement.value1 = 0xff004241; replacement.value2 = 1;
  ASSERT_EQ(radicle_plugin_set_object_template_property(plugin.handle, record, 0, 1,
      &replacement, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_INVALID_VALUE);
  replacement.value2 = 0;
  ASSERT_EQ(radicle_plugin_set_object_template_property(plugin.handle, record, 0, 1,
      &replacement, &status), RADICLE_STATUS_OK);
  EXPECT_EQ(status, RADICLE_OBJECT_TEMPLATE_UPDATED);
  const radicle_object_template_rows* rows = nullptr;
  ASSERT_EQ(radicle_plugin_object_template_rows(plugin.handle, record, 0, &rows, &status), RADICLE_STATUS_OK);
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(rows->keyword_count, 1u);
  EXPECT_EQ(rows->include_count, 1u);
  ASSERT_EQ(rows->property_count, 2u);
  EXPECT_EQ(rows->properties[0].value1, 77u);
  EXPECT_EQ(rows->properties[1].value_type, 3u);
  EXPECT_EQ(rows->properties[1].function_type, 2u);
  EXPECT_EQ(rows->properties[1].property, 9u);
  EXPECT_EQ(rows->properties[1].value1, 0xff004241u);
  EXPECT_EQ(rows->properties[1].value2, 0u);
  radicle_object_template_rows_free(rows);
  const auto output = plugin.dir / "out.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.handle, output.string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(output);
  const auto* found = fo4::find_record(saved, fo4::FormKey::parse("000800:out.esp"));
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->subrecords[1].data.size(), 77u);
  EXPECT_EQ(found->subrecords[1].data[65], std::byte{0x41});
  EXPECT_EQ(found->subrecords[1].data[67], std::byte{0});
  EXPECT_EQ(found->subrecords[1].data[68], std::byte{0xff});
}

// Detects: C callers must persist nested link edits without changing neighboring template data.
TEST(CApiMutation, ObjectTemplateLinksSurviveSaveAndReload) {
  Plugin plugin(template_bytes(), "Template.esp");
  ASSERT_NE(plugin.handle, nullptr);
  const radicle_form_key record{"Template.esp", 0x800};
  const radicle_form_key target{"Master.esm", 0x555};
  radicle_object_template_write result = RADICLE_OBJECT_TEMPLATE_INVALID_VALUE;
  const radicle_object_template_rows* rows = nullptr;
  ASSERT_EQ(radicle_plugin_object_template_rows(plugin.handle, record, 0, &rows, &result), RADICLE_STATUS_OK);
  ASSERT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_NE(rows, nullptr);
  ASSERT_EQ(rows->keyword_count, 1u);
  ASSERT_EQ(rows->include_count, 1u);
  ASSERT_EQ(rows->property_count, 1u);
  EXPECT_STREQ(rows->keywords[0].plugin, "Master.esm");
  EXPECT_EQ(rows->includes[0].optional, 1u);
  EXPECT_EQ(rows->properties[0].value_type, 4u);
  EXPECT_EQ(rows->min_level_for_ranks, 0u);
  EXPECT_EQ(rows->alt_levels_per_tier, 0u);
  radicle_object_template_rows_free(rows);
  ASSERT_EQ(radicle_plugin_set_object_template_rank_tier(plugin.handle, record, 0, 7, 8, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, target, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  const radicle_form_key empty{nullptr, 0};
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, empty, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, target, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_INCLUDE, 0, target, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_INCLUDE, 0, empty, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_INVALID_VALUE);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_PROPERTY, 0, target, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  const radicle_form_key unavailable{"Missing.esm", 0x777};
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_KEYWORD, 0, unavailable, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_INVALID_VALUE);
  ASSERT_EQ(radicle_plugin_set_object_template_link(plugin.handle, record, 0,
      RADICLE_OBJECT_TEMPLATE_INCLUDE, 1, target, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_INDEX_OUT_OF_RANGE);
  ASSERT_EQ(radicle_plugin_set_object_template_include_options(plugin.handle, record, 0, 0,
      9, 0, 1, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  ASSERT_EQ(radicle_plugin_set_object_template_property_value(plugin.handle, record, 0, 0,
      1, 42, &result), RADICLE_STATUS_OK);
  EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  for (const auto kind : {RADICLE_OBJECT_TEMPLATE_KEYWORD, RADICLE_OBJECT_TEMPLATE_INCLUDE,
                          RADICLE_OBJECT_TEMPLATE_PROPERTY}) {
    ASSERT_EQ(radicle_plugin_edit_object_template_row(plugin.handle, record, 0, kind,
        RADICLE_OBJECT_TEMPLATE_CLONE, 0, 1, &result), RADICLE_STATUS_OK);
    EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
    ASSERT_EQ(radicle_plugin_edit_object_template_row(plugin.handle, record, 0, kind,
        RADICLE_OBJECT_TEMPLATE_MOVE, 1, 0, &result), RADICLE_STATUS_OK);
    EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
    ASSERT_EQ(radicle_plugin_edit_object_template_row(plugin.handle, record, 0, kind,
        RADICLE_OBJECT_TEMPLATE_REMOVE, 0, 0, &result), RADICLE_STATUS_OK);
    EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  }
  radicle_object_template_insert_row inserted{};
  inserted.link = target;
  inserted.attach_point = 4;
  inserted.property.value_type = 0;
  inserted.property.value1 = 77;
  for (const auto kind : {RADICLE_OBJECT_TEMPLATE_KEYWORD, RADICLE_OBJECT_TEMPLATE_INCLUDE,
                          RADICLE_OBJECT_TEMPLATE_PROPERTY}) {
    ASSERT_EQ(radicle_plugin_insert_object_template_row(plugin.handle, record, 0, kind,
        1, &inserted, &result), RADICLE_STATUS_OK);
    EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
    ASSERT_EQ(radicle_plugin_edit_object_template_row(plugin.handle, record, 0, kind,
        RADICLE_OBJECT_TEMPLATE_REMOVE, 1, 0, &result), RADICLE_STATUS_OK);
    EXPECT_EQ(result, RADICLE_OBJECT_TEMPLATE_UPDATED);
  }
  const auto output = plugin.dir / "saved.esp";
  ASSERT_EQ(radicle_plugin_write_file(plugin.handle, output.string().c_str()), RADICLE_STATUS_OK);
  const auto saved = fo4::read_plugin_file(output);
  const auto* found = fo4::find_record(saved, fo4::FormKey::parse("000800:saved.esp"));
  ASSERT_NE(found, nullptr);
  const auto& bytes = found->subrecords[1].data;
  ASSERT_EQ(bytes.size(), 53u);
  EXPECT_EQ(bytes[20], std::byte{7});
  EXPECT_EQ(bytes[21], std::byte{8});
  for (const auto offset : {16u, 22u, 41u}) {
    EXPECT_EQ(bytes[offset], std::byte{0x55});
    EXPECT_EQ(bytes[offset + 1], std::byte{0x05});
    EXPECT_EQ(bytes[offset + 2], std::byte{0});
    EXPECT_EQ(bytes[offset + 3], std::byte{0});
  }
  EXPECT_EQ(bytes[26], std::byte{9});
  EXPECT_EQ(bytes[27], std::byte{0});
  EXPECT_EQ(bytes[28], std::byte{1});
  EXPECT_EQ(bytes[45], std::byte{42});
}

// Constraint: the referrer's NAME is a schema-proven FormLink, so a repoint must rewrite it.
std::vector<std::byte> master_link_plugin() {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(2);
  hedr.u32(0x802);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::write_subrecord(out, "MAST", std::string_view{"Master.esp\0", 11});
  fo4::test::ByteWriter data;
  data.u32(0);
  data.u32(0);
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, tes4);
  const auto group = fo4::test::begin_group(out, "MISC", 0);
  const auto target = fo4::test::begin_record(out, "MISC", 0, 0x01000800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Target\0", 7});
  fo4::test::end_record(out, target);
  const auto referrer = fo4::test::begin_record(out, "MISC", 0, 0x01000801);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Referrer\0", 9});
  fo4::test::ByteWriter name;
  name.u32(0x00000800);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::end_record(out, referrer);
  fo4::test::end_group(out, group);
  return out.bytes;
}

// Detects: a deep copy that renumbers the copy or drops the source owner; the copied key must keep that owner.
TEST(CApiMutation, DeepCopyKeepsTheSourceOwner) {
  Plugin source(misc_bytes(), "Source.esp");
  Plugin destination("Destination.esp");
  const radicle_form_key from{"Source.esp", 0x800};
  radicle_record_copy_status copy = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  ASSERT_EQ(radicle_plugin_copy_record_deep(destination.handle, source.handle, from, 0, &copy),
            RADICLE_STATUS_OK);
  EXPECT_EQ(copy, RADICLE_RECORD_COPY_INSERTED);

  radicle_record_copy_status again = RADICLE_RECORD_COPY_ALREADY_EXISTS;
  EXPECT_EQ(radicle_plugin_copy_record_deep(destination.handle, source.handle, from, 0, &again),
            RADICLE_STATUS_OK);
  EXPECT_EQ(again, RADICLE_RECORD_COPY_ALREADY_EXISTS);

  // The copy keeps the Source.esp owner, so a local renumber must refuse it and Destination.esp owns nothing.
  EXPECT_EQ(radicle_plugin_renumber_record(destination.handle, from, 0x900),
            RADICLE_STATUS_INVALID_ARGUMENT);
  const radicle_form_key local{"Destination.esp", 0x800};
  EXPECT_EQ(radicle_plugin_renumber_record(destination.handle, local, 0x900),
            RADICLE_STATUS_NOT_FOUND);
}

// Detects: a renumber that leaves the old key behind or refuses a legal move; the old key must be gone.
TEST(CApiMutation, RenumberMovesTheRecordAndLeavesTheOldKeyGone) {
  Plugin plugin(misc_bytes(), "Local.esp");
  const radicle_form_key from{"Local.esp", 0x800};
  ASSERT_EQ(radicle_plugin_renumber_record(plugin.handle, from, 0x900), RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, from, 0x901),
            RADICLE_STATUS_NOT_FOUND);
  const radicle_form_key moved{"Local.esp", 0x900};
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, moved, 0x900), RADICLE_STATUS_OK);
}

// Detects: an inject that keeps the old local key or accepts a non-master owner; the record must leave local space.
TEST(CApiMutation, InjectLocalRecordIntoMasterSpace) {
  Plugin plugin(master_link_plugin(), "Inject.esp");
  const radicle_form_key from{"Inject.esp", 0x800};
  const radicle_form_key target{"Master.esp", 0x850};
  ASSERT_EQ(radicle_plugin_inject_local_record(plugin.handle, from, target),
            RADICLE_STATUS_OK);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, from, 0x800),
            RADICLE_STATUS_NOT_FOUND);
  EXPECT_EQ(radicle_plugin_renumber_record(plugin.handle, target, 0x851),
            RADICLE_STATUS_INVALID_ARGUMENT);

  const radicle_form_key foreign{"Foreign.esp", 0x850};
  EXPECT_EQ(radicle_plugin_inject_local_record(plugin.handle, target, foreign),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Detects: a repoint that reports success without rewriting the FormLink; the second pass must count zero.
TEST(CApiMutation, RepointsAFormLinkAndCountsIt) {
  Plugin plugin(master_link_plugin(), "Reference.esp");
  radicle_plugin* order[1] = {plugin.handle};
  const radicle_form_key from{"Master.esp", 0x800};
  const radicle_form_key to{"Master.esp", 0x850};
  size_t count = 99;
  ASSERT_EQ(radicle_plugin_repoint_record_references(order, 1, from, to, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);

  size_t after = 99;
  ASSERT_EQ(radicle_plugin_repoint_record_references(order, 1, from, to, &after),
            RADICLE_STATUS_OK);
  EXPECT_EQ(after, 0u);

  const radicle_form_key other_owner{"Other.esp", 0x850};
  EXPECT_EQ(radicle_plugin_repoint_record_references(order, 1, from, other_owner, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
  radicle_plugin* null_order[1] = {nullptr};
  EXPECT_EQ(radicle_plugin_repoint_record_references(null_order, 1, from, to, nullptr),
            RADICLE_STATUS_INVALID_ARGUMENT);
}

// Detects: the same handle listed twice was rewritten twice and double counted; it must be refused untouched.
TEST(CApiMutation, RepointRefusesTheSamePluginListedTwice) {
  Plugin plugin(master_link_plugin(), "Reference.esp");
  radicle_plugin* twice[2] = {plugin.handle, plugin.handle};
  const radicle_form_key from{"Master.esp", 0x800};
  const radicle_form_key to{"Master.esp", 0x850};
  size_t count = 99;
  EXPECT_EQ(radicle_plugin_repoint_record_references(twice, 2, from, to, &count),
            RADICLE_STATUS_INVALID_ARGUMENT);

  radicle_plugin* once[1] = {plugin.handle};
  ASSERT_EQ(radicle_plugin_repoint_record_references(once, 1, from, to, &count),
            RADICLE_STATUS_OK);
  EXPECT_EQ(count, 1u);
}

}  // namespace
