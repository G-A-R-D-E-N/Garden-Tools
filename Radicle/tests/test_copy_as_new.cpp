#include "fo4/c_api.h"
#include "fo4/c_api_env.h"
#include "fo4/forms/copy_as_new.hpp"
#include "fo4/forms/placed_reference.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace {

void write_header(fo4::test::ByteWriter& out,
                  std::initializer_list<std::string_view> masters,
                  std::uint32_t next_form_id) {
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(0);
  hedr.u32(next_form_id);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  for (const auto master : masters) {
    std::string name(master);
    name.push_back('\0');
    fo4::test::write_subrecord(out, "MAST", std::string_view{name});
    fo4::test::ByteWriter data;
    data.u32(0);
    data.u32(0);
    fo4::test::write_subrecord(out, "DATA", data.bytes);
  }
  fo4::test::end_record(out, tes4);
}

std::vector<std::byte> header_only(std::initializer_list<std::string_view> masters,
                                   std::uint32_t next_form_id) {
  fo4::test::ByteWriter out;
  write_header(out, masters, next_form_id);
  return out.bytes;
}

std::size_t begin_group_raw(fo4::test::ByteWriter& out,
                            std::uint32_t label,
                            fo4::GroupType type) {
  const auto offset = out.size();
  out.sig("GRUP");
  out.u32(0);
  out.u32(label);
  out.i32(static_cast<std::int32_t>(type));
  out.u16(0);
  out.u16(0);
  out.u16(0);
  out.u16(0);
  return offset;
}

void end_group_raw(fo4::test::ByteWriter& out, std::size_t offset) {
  out.patch_u32(offset + 4, static_cast<std::uint32_t>(out.size() - offset));
}

void write_stat(fo4::test::ByteWriter& out,
                std::uint32_t raw,
                std::uint32_t swap_raw,
                std::string_view editor_id) {
  const auto stat = fo4::test::begin_record(out, "STAT", 0, raw);
  if (!editor_id.empty()) {
    std::string text(editor_id);
    text.push_back('\0');
    fo4::test::write_subrecord(out, "EDID", std::string_view{text});
  }
  fo4::test::ByteWriter swap;
  swap.u32(swap_raw);
  fo4::test::write_subrecord(out, "MODS", swap.bytes);
  fo4::test::end_record(out, stat);
}

std::vector<std::byte> stat_source(std::string_view editor_id) {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"}, 0x900);
  const auto top = fo4::test::begin_group(out, "STAT", 0);
  write_stat(out, 0x01000801u, 0x00000802u, editor_id);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> top_level_source(std::string_view signature) {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"}, 0x900);
  const auto top = fo4::test::begin_group(out, signature, 0);
  const auto record = fo4::test::begin_record(out, signature, 0, 0x01000801u);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> container_source(std::string_view owner_signature,
                                        std::uint32_t owner_raw,
                                        std::uint32_t union_raw) {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"}, 0x900);
  const auto top = fo4::test::begin_group(out, "CONT", 0);
  const auto container = fo4::test::begin_record(out, "CONT", 0, 0x01000800u);
  fo4::test::ByteWriter cnto;
  cnto.u32(0x01000803u);
  cnto.i32(1);
  fo4::test::write_subrecord(out, "CNTO", cnto.bytes);
  fo4::test::ByteWriter coed;
  coed.u32(owner_raw);
  coed.u32(union_raw);
  coed.f32(1.0f);
  fo4::test::write_subrecord(out, "COED", coed.bytes);
  fo4::test::end_record(out, container);
  fo4::test::end_group(out, top);
  const auto owner_group = fo4::test::begin_group(out, owner_signature, 0);
  fo4::test::end_record(out, fo4::test::begin_record(out, owner_signature, 0, 0x01000801u));
  fo4::test::end_group(out, owner_group);
  const auto glob_group = fo4::test::begin_group(out, "GLOB", 0);
  fo4::test::end_record(out, fo4::test::begin_record(out, "GLOB", 0, 0x01000802u));
  fo4::test::end_group(out, glob_group);
  return out.bytes;
}

void write_cell(fo4::test::ByteWriter& out, std::uint32_t raw) {
  const auto cell = fo4::test::begin_record(out, "CELL", 0, raw);
  const std::vector<std::byte> data{std::byte{1}, std::byte{0}};
  fo4::test::write_subrecord(out, "DATA", data);
  fo4::test::end_record(out, cell);
}

void write_reference(fo4::test::ByteWriter& out, std::uint32_t raw, std::uint32_t base_raw) {
  const auto refr = fo4::test::begin_record(out, "REFR", 0, raw);
  fo4::test::ByteWriter name;
  name.u32(base_raw);
  fo4::test::write_subrecord(out, "NAME", name.bytes);
  fo4::test::ByteWriter data;
  for (int index = 0; index < 6; ++index) {
    data.f32(10.0f + static_cast<float>(index));
  }
  fo4::test::write_subrecord(out, "DATA", data.bytes);
  fo4::test::end_record(out, refr);
}

std::vector<std::byte> interior_reference_source() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"}, 0x900);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  const auto children = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_children);
  const auto temporary = begin_group_raw(out, 0x00000800u, fo4::GroupType::cell_temporary_children);
  write_reference(out, 0x00000801u, 0x00000802u);
  end_group_raw(out, temporary);
  end_group_raw(out, children);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

std::vector<std::byte> cell_target() {
  fo4::test::ByteWriter out;
  write_header(out, {"Base.esm"}, 0x900);
  const auto top = fo4::test::begin_group(out, "CELL", 0);
  const auto block = begin_group_raw(out, 0, fo4::GroupType::interior_cell_block);
  const auto subblock = begin_group_raw(out, 0, fo4::GroupType::interior_cell_subblock);
  write_cell(out, 0x00000800u);
  end_group_raw(out, subblock);
  end_group_raw(out, block);
  fo4::test::end_group(out, top);
  return out.bytes;
}

const fo4::Subrecord* find_subrecord(const fo4::Record& record, std::string_view signature) {
  for (const auto& subrecord : record.subrecords) {
    if (subrecord.signature == fo4::Signature::from_string(signature)) return &subrecord;
  }
  return nullptr;
}

std::string edid_of(const fo4::Record& record) {
  const auto* edid = find_subrecord(record, "EDID");
  if (edid == nullptr) return {};
  const std::string_view view(reinterpret_cast<const char*>(edid->data.data()), edid->data.size());
  return std::string(view.substr(0, view.find('\0')));
}

std::uint32_t u32_field(const fo4::Record& record,
                        std::string_view signature,
                        std::size_t offset = 0) {
  const auto* subrecord = find_subrecord(record, signature);
  if (subrecord == nullptr) return 0;
  std::uint32_t value = 0;
  for (std::size_t index = 0; index < 4 && offset + index < subrecord->data.size(); ++index) {
    value |= std::to_integer<std::uint32_t>(subrecord->data[offset + index]) << (8 * index);
  }
  return value;
}

struct TempDir {
  TempDir() {
    const auto id = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("radicle_copy_as_new_" + std::to_string(id));
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  std::filesystem::path path;
};

std::filesystem::path write_file(const TempDir& dir,
                                 const std::string& name,
                                 const std::vector<std::byte>& bytes) {
  const auto path = dir.path / name;
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return path;
}

}  // namespace

TEST(CopyAsNewRecord, CopiesTopLevelRecordAndAddsTheLinkedMaster) {
  const auto source = fo4::read_plugin(stat_source("Crate"), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Other.esm"}, 0x800), "Patch.esp");
  const auto source_bytes = fo4::test::original_bytes(source);

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  EXPECT_EQ(result.new_record, fo4::FormKey::parse("000800:Patch.esp"));
  ASSERT_EQ(target.header.masters.size(), 2u);
  EXPECT_EQ(target.header.masters[1].key.filename(), "Base.esm");
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(copied->header.signature, fo4::Signature::from_string("STAT"));
  EXPECT_EQ(edid_of(*copied), "Crate");
  EXPECT_EQ(u32_field(*copied, "MODS"), 0x01000802u);
  EXPECT_EQ(fo4::test::original_bytes(source), source_bytes);
}

TEST(CopyAsNewRecord, EditorIdOptionReplacesTheCopiedEditorId) {
  const auto source = fo4::read_plugin(stat_source("Crate"), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Base.esm"}, 0x800), "Patch.esp");
  fo4::forms::RecordCopyNewOptions options;
  options.editor_id = "CrateNew";
  options.editor_id_prefix = "Ignored_";

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"), options);
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(edid_of(*copied), "CrateNew");
}

TEST(CopyAsNewRecord, PrefixAndSuffixWrapTheSourceEditorId) {
  const auto source = fo4::read_plugin(stat_source("Crate"), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Base.esm"}, 0x800), "Patch.esp");
  fo4::forms::RecordCopyNewOptions options;
  options.editor_id_prefix = "New_";
  options.editor_id_suffix = "_v2";

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"), options);
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(edid_of(*copied), "New_Crate_v2");
}

TEST(CopyAsNewRecord, RemovePrefixIgnoresCaseBeforeTheNewPrefix) {
  const auto source = fo4::read_plugin(stat_source("Old_Crate"), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Base.esm"}, 0x800), "Patch.esp");
  fo4::forms::RecordCopyNewOptions options;
  options.editor_id_remove_prefix = "old_";
  options.editor_id_prefix = "New_";

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"), options);
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(edid_of(*copied), "New_Crate");
}

TEST(CopyAsNewRecord, EditorIdOptionsSkipRecordsWithoutAnEditorId) {
  const auto source = fo4::read_plugin(stat_source(""), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Base.esm"}, 0x800), "Patch.esp");
  fo4::forms::RecordCopyNewOptions options;
  options.editor_id_prefix = "New_";

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"), options);
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(find_subrecord(*copied, "EDID"), nullptr);
}

TEST(CopyAsNewRecord, DelegatesContainedReferenceToThePlacedCopy) {
  const auto source = fo4::read_plugin(interior_reference_source(), "Source.esp");
  auto target = fo4::read_plugin(cell_target(), "Patch.esp");
  const auto cell = fo4::FormKey::parse("000800:Base.esm");

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Base.esm"));
  EXPECT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  EXPECT_EQ(result.new_record.owner().filename(), "Patch.esp");
  EXPECT_NE(fo4::find_record(target, result.new_record), nullptr);
  EXPECT_EQ(fo4::forms::placed_references_for_cell(target, cell).size(), 1u);
}

void expect_refused_before_writing(std::string_view signature) {
  const auto source = fo4::read_plugin(top_level_source(signature), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Other.esm"}, 0x800), "Patch.esp");

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000801:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::RecordCopyNewStatus::unsupported_signature);
  EXPECT_EQ(target.header.masters.size(), 1u);
  EXPECT_EQ(target.header.next_form_id, 0x800u);
}

TEST(CopyAsNewRecord, RefusesPlayerAndNoiseRecordsBeforeWriting) {
  expect_refused_before_writing("PLYR");
  expect_refused_before_writing("NOCM");
}

TEST(CopyAsNewRecord, ReportsAMissingSource) {
  const auto source = fo4::read_plugin(stat_source("Crate"), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Base.esm"}, 0x800), "Patch.esp");

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000999:Source.esp"));
  EXPECT_EQ(result.status, fo4::forms::RecordCopyNewStatus::source_missing);
}

TEST(CopyAsNewRecordCApi, CopiesThroughTheHandlesAndWritesTheResult) {
  TempDir dir;
  const auto source_path = write_file(dir, "Source.esp", stat_source("Crate"));
  const auto target_path = write_file(dir, "Patch.esp", header_only({"Other.esm"}, 0x800));
  const auto out_path = dir.path / "Out.esp";

  radicle_plugin* source = nullptr;
  radicle_plugin* target = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(source_path.string().c_str(), &source), RADICLE_STATUS_OK);
  ASSERT_EQ(radicle_plugin_read_file(target_path.string().c_str(), &target), RADICLE_STATUS_OK);

  const radicle_form_key record{"Source.esp", 0x801};
  radicle_form_key created{nullptr, 0};
  EXPECT_EQ(radicle_plugin_copy_record_as_new(
                target, source, record, "CrateNew", nullptr, nullptr, nullptr, nullptr, &created),
            RADICLE_STATUS_OK);
  EXPECT_STREQ(created.plugin, "Patch.esp");
  EXPECT_EQ(created.local_id, 0x800u);

  ASSERT_EQ(radicle_plugin_write_file(target, out_path.string().c_str()), RADICLE_STATUS_OK);
  const auto patch = fo4::read_plugin_file(out_path);
  const auto* copied = fo4::find_record(patch, fo4::FormKey::parse("000800:Out.esp"));
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(edid_of(*copied), "CrateNew");

  radicle_form_key missing{nullptr, 0};
  const radicle_form_key absent{"Source.esp", 0x999};
  EXPECT_EQ(radicle_plugin_copy_record_as_new(
                target, source, absent, nullptr, nullptr, nullptr, "New_", nullptr, &missing),
            RADICLE_STATUS_NOT_FOUND);

  radicle_plugin_destroy(source);
  radicle_plugin_destroy(target);
}

TEST(CopyAsNewRecord, CopiesAContainerWhoseCoedOwnerIsAnNpcAndRemapsTheGlobal) {
  const auto source = fo4::read_plugin(container_source("NPC_", 0x01000801u, 0x01000802u), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Other.esm", "Base.esm"}, 0x800), "Patch.esp");

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000800:Source.esp"));
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  ASSERT_EQ(target.header.masters.size(), 3u);
  EXPECT_EQ(target.header.masters[2].key.filename(), "Source.esp");
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(u32_field(*copied, "COED", 0), 0x02000801u);
  EXPECT_EQ(u32_field(*copied, "COED", 4), 0x02000802u);
}

TEST(CopyAsNewRecord, CopiesAContainerWhoseCoedOwnerIsAFactionAndKeepsTheRank) {
  const auto source = fo4::read_plugin(container_source("FACT", 0x01000801u, 3u), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Other.esm", "Base.esm"}, 0x800), "Patch.esp");

  const auto result = fo4::forms::copy_record_as_new(
      target, source, fo4::FormKey::parse("000800:Source.esp"));
  ASSERT_EQ(result.status, fo4::forms::RecordCopyNewStatus::created);
  const auto* copied = fo4::find_record(target, result.new_record);
  ASSERT_NE(copied, nullptr);
  EXPECT_EQ(u32_field(*copied, "COED", 0), 0x02000801u);
  EXPECT_EQ(u32_field(*copied, "COED", 4), 3u);
}

TEST(CopyAsNewRecord, RefusesACoedOwnerThatNoPluginDefines) {
  const auto source = fo4::read_plugin(container_source("NPC_", 0x00000802u, 3u), "Source.esp");
  auto target = fo4::read_plugin(header_only({"Other.esm", "Base.esm"}, 0x800), "Patch.esp");

  EXPECT_THROW(
      fo4::forms::copy_record_as_new(target, source, fo4::FormKey::parse("000800:Source.esp")),
      fo4::FormIdError);
}
