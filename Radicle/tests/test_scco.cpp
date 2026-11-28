#include "fo4/forms/scco.hpp"
#include "fo4/plugin/reader.hpp"
#include "plugin_bytes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view k_plugin = "Scco.esp";

fo4::FormKey key(std::uint32_t local) {
  return fo4::FormKey{fo4::PluginKey::from_filename(k_plugin), fo4::LocalFormId{local & 0xFFFFFF}};
}

std::vector<std::byte> u32_bytes(std::uint32_t value) {
  fo4::test::ByteWriter out;
  out.u32(value);
  return out.bytes;
}

std::vector<std::byte> xnam_bytes(std::int32_t x, std::int32_t y) {
  fo4::test::ByteWriter out;
  out.i32(x);
  out.i32(y);
  return out.bytes;
}

fo4::Plugin build_plugin(bool malformed_run) {
  fo4::test::ByteWriter out;
  const auto tes4 = fo4::test::begin_record(out, "TES4", 0, 0);
  fo4::test::ByteWriter hedr;
  hedr.f32(1.0f);
  hedr.u32(3);
  hedr.u32(0x900);
  fo4::test::write_subrecord(out, "HEDR", hedr.bytes);
  fo4::test::end_record(out, tes4);

  const auto scen_group = fo4::test::begin_group(out, "SCEN", 0);
  for (const std::uint32_t local : {0x801u, 0x802u}) {
    const auto scen = fo4::test::begin_record(out, "SCEN", 0, local);
    fo4::test::write_subrecord(out, "EDID", std::string_view{"Scene\0", 6});
    fo4::test::end_record(out, scen);
  }
  fo4::test::end_group(out, scen_group);

  const auto scco_group = fo4::test::begin_group(out, "SCCO", 0);
  const auto scco = fo4::test::begin_record(out, "SCCO", 0, 0x800);
  fo4::test::write_subrecord(out, "EDID", std::string_view{"Layout\0", 7});
  fo4::test::write_subrecord(out, "QNAM", u32_bytes(0x800));
  fo4::test::write_subrecord(out, "SNAM", u32_bytes(0x801));
  if (!malformed_run) fo4::test::write_subrecord(out, "XNAM", xnam_bytes(10, 20));
  fo4::test::write_subrecord(out, "VNAM", u32_bytes(1));
  fo4::test::write_subrecord(out, "XNAM", xnam_bytes(10, 20));
  fo4::test::end_record(out, scco);
  fo4::test::end_group(out, scco_group);
  return fo4::read_plugin(out.bytes, std::string(k_plugin));
}

std::vector<std::string> signatures(const fo4::Plugin& plugin, const fo4::FormKey& form) {
  std::vector<std::string> out;
  const auto* record = fo4::find_record(plugin, form);
  if (record == nullptr) return out;
  for (const auto& subrecord : record->subrecords) out.push_back(subrecord.signature.to_string());
  return out;
}

std::int32_t read_i32(const fo4::Subrecord& subrecord, std::size_t offset) {
  std::int32_t value = 0;
  std::memcpy(&value, subrecord.data.data() + offset, sizeof(value));
  return value;
}

TEST(SccoEdit, AddInsertsOneSnamXnamPairBeforeTheVnamMarker) {
  auto plugin = build_plugin(false);
  ASSERT_TRUE(fo4::forms::add_scco_scene(plugin, key(0x800), key(0x802), -5, 7));

  const std::vector<std::string> expected{"EDID", "QNAM", "SNAM", "XNAM", "SNAM", "XNAM", "VNAM", "XNAM"};
  EXPECT_EQ(signatures(plugin, key(0x800)), expected);
  const auto* record = fo4::find_record(plugin, key(0x800));
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->subrecords[4].data, u32_bytes(0x802));
  EXPECT_EQ(read_i32(record->subrecords[5], 0), -5);
  EXPECT_EQ(read_i32(record->subrecords[5], 4), 7);
}

TEST(SccoEdit, RemoveErasesOnlyThePairAndKeepsTheTail) {
  auto plugin = build_plugin(false);
  ASSERT_TRUE(fo4::forms::remove_scco_scene(plugin, key(0x800), 0));

  const std::vector<std::string> expected{"EDID", "QNAM", "VNAM", "XNAM"};
  EXPECT_EQ(signatures(plugin, key(0x800)), expected);
}

TEST(SccoEdit, MissingRecordOrPairReturnsFalseAndChangesNothing) {
  auto plugin = build_plugin(false);
  EXPECT_FALSE(fo4::forms::add_scco_scene(plugin, key(0x8FF), key(0x801), 0, 0));
  EXPECT_FALSE(fo4::forms::remove_scco_scene(plugin, key(0x8FF), 0));
  EXPECT_FALSE(fo4::forms::remove_scco_scene(plugin, key(0x800), 1));
  EXPECT_EQ(signatures(plugin, key(0x800)).size(), 6u);
}

TEST(SccoEdit, RefusalsThrowAndLeaveThePluginUntouched) {
  auto plugin = build_plugin(false);
  EXPECT_THROW(static_cast<void>(fo4::forms::add_scco_scene(plugin, key(0x800), key(0x8FF), 0, 0)),
               fo4::forms::SccoError);
  EXPECT_THROW(static_cast<void>(fo4::forms::add_scco_scene(plugin, key(0x800), key(0x800), 0, 0)),
               fo4::forms::SccoError);
  EXPECT_EQ(signatures(plugin, key(0x800)).size(), 6u);

  auto malformed = build_plugin(true);
  EXPECT_THROW(static_cast<void>(fo4::forms::add_scco_scene(malformed, key(0x800), key(0x801), 0, 0)),
               fo4::forms::SccoError);
  EXPECT_THROW(static_cast<void>(fo4::forms::remove_scco_scene(malformed, key(0x800), 0)),
               fo4::forms::SccoError);
}

}  // namespace
