#include "fo4/archive/ba2.hpp"
#include "fo4/archive/ba2_reader.hpp"
#include "fo4/archive/ba2_texture.hpp"
#include "ba2_fixture.hpp"
#include "corruption_harness.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>

namespace {

constexpr std::uint32_t k_magic = 0x58445442u;  // BTDX
constexpr std::uint32_t k_gnrl = 0x4C524E47u;   // GNRL
constexpr std::uint32_t k_chunk_sentinel = 0xBAADF00Du;

void u64(fo4::test::ByteWriter& out, std::uint64_t value) {
  out.u32(static_cast<std::uint32_t>(value));
  out.u32(static_cast<std::uint32_t>(value >> 32));
}

fo4::test::ByteWriter header(std::uint32_t file_count,
                             std::uint64_t string_table_offset) {
  fo4::test::ByteWriter out;
  out.u32(k_magic);
  out.u32(1);
  out.u32(k_gnrl);
  out.u32(file_count);
  u64(out, string_table_offset);
  return out;
}

void entry_header(fo4::test::ByteWriter& out, std::uint8_t chunk_count) {
  out.u32(0);  // name hash
  out.u32(0);  // extension hash
  out.u32(0);  // directory hash
  out.u8(0);   // unknown
  out.u8(chunk_count);
  out.u16(0x10);
}

}

TEST(Ba2Hardening, ForgedHugeFileCountFailsBeforeEntryAllocation) {
  const auto out = header(std::numeric_limits<std::uint32_t>::max(), 0);
  EXPECT_THROW(
      static_cast<void>(fo4::archive::read_ba2_general(out.bytes)),
      fo4::archive::Ba2Error);
}

TEST(Ba2Hardening, TruncatedChunkTableUsesBa2ErrorContract) {
  auto out = header(1, 0);
  entry_header(out, 255);
  EXPECT_THROW(
      static_cast<void>(fo4::archive::read_ba2_general(out.bytes)),
      fo4::archive::Ba2Error);
}

TEST(Ba2Hardening, TruncatedStringTableUsesBa2ErrorContract) {
  auto out = header(1, 40);
  entry_header(out, 0);
  ASSERT_EQ(out.bytes.size(), 40u);

  try {
    static_cast<void>(fo4::archive::read_ba2_general(out.bytes));
    FAIL() << "expected truncated BA2 string table to fail";
  } catch (const fo4::archive::Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("BA2 parse error"), std::string::npos);
  } catch (...) {
    FAIL() << "truncated BA2 escaped the Ba2Error API contract";
  }
}

TEST(Ba2Hardening, CompressedChunkBeyondZlibBoundFailsBeforeAllocation) {
  auto out = header(1, 0);
  entry_header(out, 1);
  u64(out, 60);
  out.u32(8);
  out.u32(1u << 30);
  out.u32(k_chunk_sentinel);
  out.u32(0);
  out.u32(0);
  const auto archive = fo4::archive::read_ba2_general(out.bytes);
  try {
    static_cast<void>(fo4::archive::extract_ba2_general_entry(archive.entries.at(0)));
    FAIL() << "expected 1 GiB claim from 8 packed bytes to fail";
  } catch (const fo4::archive::Ba2Error& error) {
    EXPECT_NE(std::string(error.what()).find("expansion bound"), std::string::npos);
  }
}

namespace {

std::vector<std::byte> sweep_payload(std::size_t size, std::uint8_t fill) {
  return std::vector<std::byte>(size, std::byte{fill});
}

std::vector<std::byte> general_sample() {
  fo4::archive::Ba2GeneralArchive archive;
  archive.entries.push_back(ba2_fixture::general("meshes\\a.nif", "stored payload"));
  archive.entries.push_back(ba2_fixture::general("scripts\\b.pex", std::string(96, 'Z'), true));
  auto two_chunks = ba2_fixture::general("interface\\c.bin", "head");
  two_chunks.chunks.push_back(fo4::archive::make_compressed_ba2_chunk(sweep_payload(64, 0x33)));
  archive.entries.push_back(std::move(two_chunks));
  return fo4::archive::write_ba2_general(archive);
}

std::vector<std::byte> texture_sample() {
  fo4::archive::Ba2TextureArchive archive;
  fo4::archive::Ba2TextureEntry entry;
  entry.path = "textures\\t.dds";
  entry.texture = {4, 4, 1, 71, 0, 8};
  entry.chunks.push_back(fo4::archive::make_stored_ba2_texture_chunk(sweep_payload(16, 0x5A)));
  archive.entries.push_back(entry);
  entry.path = "textures\\u.dds";
  entry.chunks.clear();
  entry.chunks.push_back(fo4::archive::make_compressed_ba2_texture_chunk(sweep_payload(128, 0x11)));
  archive.entries.push_back(std::move(entry));
  return fo4::archive::write_ba2_texture(archive);
}

}  // namespace

// Constraint: a corrupted size, count or offset in a general BA2 must end in Ba2Error, never a read past its buffer.
TEST(Ba2Hardening, CorruptedGeneralArchivesStayInsideTheBa2ErrorContract) {
  const auto stats = fo4::test::expect_closed_under_corruption<fo4::archive::Ba2Error>(
      general_sample(), 24, [](const std::vector<std::byte>& bytes) {
        const auto archive = fo4::archive::read_ba2_general(bytes);
        for (const auto& entry : archive.entries) {
          static_cast<void>(fo4::archive::extract_ba2_general_entry(entry));
          static_cast<void>(fo4::archive::find_ba2_general_entry(archive, entry.path));
        }
      });
  EXPECT_GT(stats.rejected, 0u);
}

// Constraint: a corrupted texture header or chunk record must end in Ba2Error, never a read past its buffer.
TEST(Ba2Hardening, CorruptedTextureArchivesStayInsideTheBa2ErrorContract) {
  const auto stats = fo4::test::expect_closed_under_corruption<fo4::archive::Ba2Error>(
      texture_sample(), 24, [](const std::vector<std::byte>& bytes) {
        const auto archive = fo4::archive::read_ba2_texture(bytes);
        for (const auto& entry : archive.entries) {
          static_cast<void>(fo4::archive::extract_ba2_texture_entry(entry));
        }
      });
  EXPECT_GT(stats.rejected, 0u);
}

namespace {

class VectorSource final : public fo4::archive::Ba2ByteSource {
public:
  explicit VectorSource(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}
  std::uint64_t size() const noexcept override { return bytes_.size(); }
  void read(std::uint64_t offset, std::span<std::byte> out) const override {
    if (offset > bytes_.size() || out.size() > bytes_.size() - offset) {
      throw fo4::archive::Ba2Error("test source truncated");
    }
    std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
  }

private:
  std::vector<std::byte> bytes_;
};

void open_and_extract_every_entry(const std::vector<std::byte>& bytes) {
  const auto snapshot = fo4::archive::Ba2Snapshot::open(std::make_shared<VectorSource>(bytes));
  for (std::size_t i = 0; i < snapshot.entries().size(); ++i) {
    static_cast<void>(snapshot.extract(i, 1u << 20));
    static_cast<void>(snapshot.find(snapshot.entries()[i].path));
  }
}

}  // namespace

// Constraint: a corrupted index or chunk record in the snapshot reader must end in Ba2Error, never a read outside its source.
TEST(Ba2Hardening, CorruptedArchivesStayInsideTheBa2ErrorContractThroughTheSnapshotReader) {
  for (const auto& sample : {general_sample(), texture_sample()}) {
    const auto stats = fo4::test::expect_closed_under_corruption<fo4::archive::Ba2Error>(
        sample, 24, open_and_extract_every_entry);
    EXPECT_GT(stats.rejected, 0u);
  }
}
