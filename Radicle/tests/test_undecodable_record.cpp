#include "fo4/c_api.h"
#include "fo4/error.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

constexpr std::uint32_t k_valid_compressed_id = 0x00000801;
constexpr std::uint32_t k_corrupt_id = 0x00000802;
constexpr std::uint32_t k_plain_id = 0x00000803;

std::vector<std::byte> not_zlib() {
  fo4::test::ByteWriter stream;
  stream.raw(std::string_view{"not-zlib-data-at-all"});
  return stream.bytes;
}

void add_compressed(fo4::test::ByteWriter& out, std::uint32_t form_id, std::uint32_t declared_size,
                    std::span<const std::byte> stream) {
  const auto rec = fo4::test::begin_record(out, "MISC", fo4::RecordFlags::compressed, form_id);
  out.u32(declared_size);
  out.raw(stream);
  fo4::test::end_record(out, rec);
}

std::vector<std::byte> mixed_plugin() {
  fo4::test::ByteWriter inner;
  fo4::test::write_subrecord(inner, "EDID", std::string_view{"Valid\0", 6});
  const auto zlib = fo4::test::zlib_compress(inner.bytes);
  const auto corrupt = not_zlib();

  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  add_compressed(out, k_corrupt_id, 32, corrupt);
  add_compressed(out, k_valid_compressed_id, static_cast<std::uint32_t>(inner.size()), zlib);
  const auto plain = fo4::test::begin_record(out, "MISC", 0, k_plain_id);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Plain\0", 6});
  fo4::test::end_record(out, plain);
  fo4::test::end_group(out, grup);
  return out.bytes;
}

struct TempRoot {
  std::filesystem::path path;

  TempRoot()
      : path(std::filesystem::temp_directory_path() /
             ("radicle_undecodable_" +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::filesystem::create_directories(path);
  }
  ~TempRoot() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

}  // namespace

TEST(UndecodableRecord, CorruptCompressedRecordLoadsWithRawBytesKept) {
  const auto bytes = mixed_plugin();
  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{k_corrupt_id});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
  EXPECT_TRUE(record->compressed);
  EXPECT_TRUE(record->subrecords.empty());
  EXPECT_EQ(fo4::test::record_original_bytes(plugin, *record).size(),
            fo4::k_record_header_size + record->header.data_size);
}

TEST(UndecodableRecord, ValidSiblingsOfUndecodableRecordStillDecode) {
  auto plugin = fo4::read_plugin(mixed_plugin());
  for (const auto form_id : {k_valid_compressed_id, k_plain_id}) {
    const auto* record = fo4::find_record(plugin, fo4::RawFormId{form_id});
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->undecodable);
    ASSERT_EQ(record->subrecords.size(), 1u);
    EXPECT_EQ(record->subrecords[0].signature, fo4::sig_edid);
  }
}

TEST(UndecodableRecord, DirtyPluginWritesUndecodableRecordBytesUnchanged) {
  const auto bytes = mixed_plugin();
  auto plugin = fo4::read_plugin(bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{k_corrupt_id});
  ASSERT_NE(record, nullptr);
  const auto raw = fo4::test::record_original_bytes(plugin, *record);
  plugin.original_range.clear();
  const auto written = fo4::write_plugin(plugin);
  EXPECT_NE(std::search(written.begin(), written.end(), raw.begin(), raw.end()), written.end());
  EXPECT_EQ(written, bytes);
}

TEST(UndecodableRecord, WriterRefusesUndecodableRecordWithoutOriginalBytes) {
  auto plugin = fo4::read_plugin(mixed_plugin());
  auto& record = std::get<fo4::Record>(plugin.groups.front().children.front());
  ASSERT_EQ(record.header.form_id.value(), k_corrupt_id);
  ASSERT_TRUE(record.undecodable);
  record.original_range.clear();
  plugin.original_range.clear();
  EXPECT_THROW(fo4::write_plugin(plugin), std::invalid_argument);
}

TEST(UndecodableRecord, RemapRefusesPluginWithUndecodableRecord) {
  const auto bytes = mixed_plugin();
  auto plugin = fo4::read_plugin(bytes);
  ASSERT_NE(fo4::find_record(plugin, fo4::RawFormId{k_corrupt_id}), nullptr);
  try {
    static_cast<void>(fo4::remap_plugin_masters(
        plugin, std::vector<fo4::PluginKey>{fo4::PluginKey::from_filename("Fallout4.esm")}));
    FAIL() << "remap accepted an undecodable record";
  } catch (const std::invalid_argument& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("1 undecodable record"), std::string::npos) << message;
    EXPECT_NE(message.find("000802:"), std::string::npos) << message;
  }
  EXPECT_EQ(fo4::test::original_bytes(plugin), bytes);
}

TEST(UndecodableRecord, CApiRemapRefusesUndecodableRecordWithInvalidArgument) {
  TempRoot root;
  const auto path = root.path / "Mixed.esp";
  const auto bytes = mixed_plugin();
  {
    std::ofstream out{path, std::ios::binary};
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }

  radicle_plugin* plugin = nullptr;
  ASSERT_EQ(radicle_plugin_read_file(path.string().c_str(), &plugin), RADICLE_STATUS_OK)
      << radicle_last_error();
  const radicle_master_descriptor masters[] = {{"Fallout4.esm", RADICLE_PLUGIN_FULL}};
  EXPECT_EQ(radicle_plugin_remap_masters(plugin, masters, 1), RADICLE_STATUS_INVALID_ARGUMENT);
  EXPECT_NE(std::string(radicle_last_error()).find("undecodable record"), std::string::npos);
  radicle_plugin_destroy(plugin);
}

TEST(UndecodableRecord, CorruptCompressedTes4StillFailsThePlugin) {
  const auto corrupt = not_zlib();
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", fo4::RecordFlags::compressed, 0);
  out.u32(32);
  out.raw(corrupt);
  fo4::test::end_record(out, tes4);
  EXPECT_THROW(fo4::read_plugin(out.bytes), fo4::parse_error);
}

TEST(UndecodableRecord, DeclaredSizeBeyondExpansionBoundLoadsUndecodable) {
  const auto corrupt = not_zlib();
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::end_record(out, tes4);
  const auto grup = fo4::test::begin_group(out, "MISC", 0);
  add_compressed(out, k_corrupt_id, 0xFFFFFFFFu, corrupt);
  fo4::test::end_group(out, grup);
  auto plugin = fo4::read_plugin(out.bytes);
  const auto* record = fo4::find_record(plugin, fo4::RawFormId{k_corrupt_id});
  ASSERT_NE(record, nullptr);
  EXPECT_TRUE(record->undecodable);
}
