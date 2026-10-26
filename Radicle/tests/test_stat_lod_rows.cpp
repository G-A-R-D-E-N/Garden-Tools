#include "fo4/plugin/mutation.hpp"
#include "fo4/plugin/reader.hpp"
#include "fo4/plugin/writer.hpp"
#include "fo4/schema/scalar.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
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
constexpr std::size_t k_slot = 260;

Bytes zstr(std::string_view text) {
  ByteWriter out;
  out.raw(text);
  out.u8(0);
  return out.bytes;
}

Bytes mesh_slot(std::string_view text) {
  Bytes out = zstr(text);
  out.resize(k_slot, std::byte{0});
  return out;
}

Bytes mnam(std::initializer_list<std::string_view> meshes) {
  ByteWriter out;
  for (const auto mesh : meshes) {
    const auto slot = mesh_slot(mesh);
    out.raw(std::span<const std::byte>(slot));
  }
  return out.bytes;
}

Bytes make_plugin(std::span<const std::byte> payload) {
  ByteWriter out;
  out.bytes = fo4::test::make_tes4_plugin(0, 1.0f, {"Fallout4.esm"});
  const auto group = fo4::test::begin_group(out, "STAT", 0);
  const auto record = fo4::test::begin_record(out, "STAT", 0, 0x01000800u);
  fo4::test::write_subrecord(out, "EDID", zstr("RadicleLod"));
  fo4::test::write_subrecord(out, "MNAM", payload);
  fo4::test::end_record(out, record);
  fo4::test::end_group(out, group);
  return out.bytes;
}

fo4::schema::ScalarFieldDescriptor row(std::string_view path) {
  const auto field = fo4::schema::scalar_field_descriptor(fo4::Signature::from_string("STAT"), path);
  EXPECT_TRUE(field.has_value()) << path;
  return field.value_or(fo4::schema::ScalarFieldDescriptor{});
}

}  // namespace

TEST(StatLodRows, RowsUseTheXEditForwardStringLayout) {
  const std::array<std::string_view, 4> paths{
      "STAT\\DistantLodLevel0", "STAT\\DistantLodLevel1",
      "STAT\\DistantLodLevel2", "STAT\\DistantLodLevel3"};
  const std::array<std::size_t, 4> offsets{0, 260, 520, 780};
  const std::array<std::size_t, 4> floors{260, 520, 780, 1040};

  for (std::size_t level = 0; level < paths.size(); ++level) {
    const auto field = row(paths[level]);
    EXPECT_EQ(field.scalar.subrecord, fo4::Signature::from_string("MNAM")) << paths[level];
    EXPECT_EQ(field.scalar.storage, fo4::schema::ScalarStorage::zstring) << paths[level];
    EXPECT_TRUE(field.scalar.embedded) << paths[level];
    EXPECT_EQ(field.scalar.offset, offsets[level]) << paths[level];
    EXPECT_EQ(field.scalar.minimum_payload_size, floors[level]) << paths[level];
    EXPECT_EQ(field.scalar.payload_size, std::size_t{0}) << paths[level];
    EXPECT_FALSE(field.scalar.address.repeated) << paths[level];
  }

  EXPECT_FALSE(fo4::schema::scalar_descriptor(fo4::Signature::from_string("MNAM")).has_value());
}

TEST(StatLodRows, ReadsEachSlotUpToItsTerminator) {
  auto plugin = fo4::read_plugin(
      make_plugin(mnam({"meshes\\lod\\first.nif", "meshes\\lod\\second.nif"})), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto* found = fo4::find_record(plugin, record);
  ASSERT_NE(found, nullptr);
  ASSERT_EQ(found->subrecords.size(), std::size_t{2});
  const auto& payload = found->subrecords[1].data;
  ASSERT_EQ(payload.size(), k_slot * 2);

  const std::array<std::pair<std::string_view, std::string_view>, 2> decoded{{
      {"STAT\\DistantLodLevel0", "meshes\\lod\\first.nif"},
      {"STAT\\DistantLodLevel1", "meshes\\lod\\second.nif"},
  }};
  for (const auto& [path, mesh] : decoded) {
    const auto field = row(path);
    const auto bytes = fo4::schema::scalar_field_bytes(payload, field.scalar);
    ASSERT_FALSE(bytes.empty()) << path;
    EXPECT_EQ(fo4::schema::decode_scalar(bytes, field.scalar), mesh) << path;
    EXPECT_EQ(fo4::record_field_count(plugin, record, field), std::optional<std::size_t>{1}) << path;
  }

  for (const char* path : {"STAT\\DistantLodLevel2", "STAT\\DistantLodLevel3"}) {
    const auto field = row(path);
    EXPECT_TRUE(fo4::schema::scalar_field_bytes(payload, field.scalar).empty()) << path;
  }
}

TEST(StatLodRows, OneSlotExposesOnlyTheFirstLevel) {
  const auto payload = mnam({"meshes\\lod\\only.nif"});
  ASSERT_EQ(payload.size(), k_slot);

  const auto first = row("STAT\\DistantLodLevel0");
  EXPECT_EQ(fo4::schema::decode_scalar(fo4::schema::scalar_field_bytes(payload, first.scalar),
                                       first.scalar),
            "meshes\\lod\\only.nif");

  for (const char* path : {"STAT\\DistantLodLevel1", "STAT\\DistantLodLevel2", "STAT\\DistantLodLevel3"}) {
    const auto field = row(path);
    EXPECT_TRUE(fo4::schema::scalar_field_bytes(payload, field.scalar).empty()) << path;
  }

  const Bytes short_payload(payload.begin(), payload.begin() + k_slot - 1);
  EXPECT_TRUE(fo4::schema::scalar_field_bytes(short_payload, first.scalar).empty());
}

TEST(StatLodRows, ASlotWithoutATerminatorFailsClosed) {
  auto payload = mnam({"meshes\\lod\\unterminated.nif"});
  ASSERT_EQ(payload.size(), k_slot);
  payload.assign(k_slot, std::byte{'x'});

  const auto first = row("STAT\\DistantLodLevel0");
  EXPECT_THROW(fo4::schema::scalar_field_bytes(payload, first.scalar), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::decode_scalar(payload, first.scalar), fo4::schema::ScalarError);
}

TEST(StatLodRows, SlotSearchStopsAtTheSlotEnd) {
  Bytes payload(k_slot, std::byte{'x'});
  const auto tail = zstr("abc");
  payload.insert(payload.end(), tail.begin(), tail.end());
  payload.resize(k_slot * 2, std::byte{0});
  ASSERT_EQ(payload.size(), k_slot * 2);

  const auto first = row("STAT\\DistantLodLevel0");
  EXPECT_THROW(fo4::schema::scalar_field_bytes(payload, first.scalar), fo4::schema::ScalarError);
  EXPECT_THROW(fo4::schema::decode_scalar(payload, first.scalar), fo4::schema::ScalarError);

  const auto second = row("STAT\\DistantLodLevel1");
  const auto bytes = fo4::schema::scalar_field_bytes(payload, second.scalar);
  ASSERT_EQ(bytes.size(), std::size_t{4});
  EXPECT_EQ(fo4::schema::decode_scalar(bytes, second.scalar), "abc");
}

TEST(StatLodRows, RefusesGenericWritesAndKeepsTheBytes) {
  auto plugin = fo4::read_plugin(
      make_plugin(mnam({"meshes\\lod\\first.nif", "meshes\\lod\\second.nif"})), "Patch.esp");
  const auto record = fo4::FormKey::parse(k_key);
  const auto before = fo4::write_plugin(plugin);

  EXPECT_THROW(
      fo4::set_record_string(plugin, record, row("STAT\\DistantLodLevel0"), 0, "meshes\\other.nif"),
      fo4::schema::ScalarError);
  EXPECT_THROW(
      fo4::set_record_string(plugin, record, row("STAT\\DistantLodLevel1"), 0, "meshes\\other.nif"),
      fo4::schema::ScalarError);
  EXPECT_THROW(fo4::set_record_scalar(plugin, record, row("STAT\\DistantLodLevel0"), 0, "1"),
               fo4::schema::ScalarError);

  EXPECT_EQ(fo4::write_plugin(plugin), before);
}
